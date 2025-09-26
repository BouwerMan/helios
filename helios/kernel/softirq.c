#include "kernel/softirq.h"
#include "kernel/assert.h"
#include "kernel/atomic.h"
#include "kernel/tasks/scheduler.h"
#include "kernel/time.h"

#include <uapi/helios/errno.h>

static struct softirq g_softirqs[NUM_SOFTIRQS] = { 0 };

atomic64_t g_pending_bits = ATOMIC64_INIT(0);
bool in_progress = false;

struct task* ksoftirqd = nullptr;

static void ksoftirqd_entry();

void softirq_init(void)
{
	ksoftirqd = kthread_create("ksoftirqd", ksoftirqd_entry);
	kthread_run(ksoftirqd);
}

void do_softirq(size_t item_budget, u64 ns_budget)
{
	if (in_progress) {
		// Prevent re-entrancy
		return;
	}

	in_progress = true;

	u64 deadline = clock_now_ns() + ns_budget;

	do {
		u64 pending = (u64)atomic64_xchg_acq_rel(&g_pending_bits, 0);
		if (pending == 0) {
			break;
		}

		u64 requeue = 0;
		for (int i = 0; i < NUM_SOFTIRQS; i++) {
			if (!CHECK_BIT(pending, i)) continue;
			if (!g_softirqs[i].fn) continue;

			softirq_ret_t res =
				g_softirqs[i].fn(&item_budget, ns_budget);

			switch (res) {
			case SOFTIRQ_DONE: break;
			case SOFTIRQ_MORE: SET_BIT(requeue, i); break;
			case SOFTIRQ_PUNT:
				SET_BIT(requeue, i);
				task_wake(ksoftirqd);
				break;
			}
		}

		if (requeue != 0) {
			atomic64_fetch_or_release(&g_pending_bits,
						  (long)requeue);
		}
	} while (item_budget > 0 && clock_now_ns() < deadline);

	in_progress = false;
}

void softirq_raise(int id)
{
	if (id < 0 || id >= NUM_SOFTIRQS) {
		return;
	}

	atomic64_fetch_or_release(&g_pending_bits, (long)BIT(id));
}

int softirq_register(int id, const char* name, softirq_fn fn)
{
	if (!name || !fn || id > NUM_SOFTIRQS || id < 0) {
		return -EINVAL;
	}

	if (g_softirqs[id].fn != nullptr) {
		return -EEXIST; // Already registered
	}

	g_softirqs[id] = (struct softirq) { name, fn };

	return 0;
}

static void ksoftirqd_entry()
{
	log_debug("ksoftirqd started");
	yield_blocked(); // Yield once to let init finish
	while (true) {
		do_softirq(1024, 10UL * 1000UL * 1000UL); // 10ms
		yield_blocked();
	}
}
