#include <kernel/liballoc.h>
#include <kernel/memory/vmm.h>
#include <kernel/tasks/scheduler.h>
#include <util/log.h>

volatile bool need_reschedule = false;
static bool initialized = false;

struct scheduler_queue queue = { 0 };

extern void __switch_to(struct registers* new);

void check_reschedule(struct registers* regs)
{
	if (need_reschedule && initialized) {
		need_reschedule = false;

		// TODO: I'm doing some weird shit here, make this cleaner
		queue.current_task->regs = regs;
		queue.current_task->state = READY;

		struct task* new = scheduler_pick_next();
		if (new == NULL) return;

		new->state = RUNNING;

		__switch_to(new->regs);
	}
}

struct task* task_add(void)
{
	struct task* task = kmalloc(sizeof(struct task));
	if (task == NULL) return NULL;

	task->PID = queue.pid_i++;
	if (queue.list == 0) {
		log_debug("Initializing new list");
		list_init(&task->list);
		queue.list = &task->list;
	} else {
		log_debug("Appending new task to list");
		list_append(queue.list, &task->list);
	}

	log_debug("Initialized list");
	return task;
}

void init_scheduler(void)
{
	struct task* kernel_task = task_add();
	kernel_task->cr3 = vmm_read_cr3();
	kernel_task->parent = kernel_task;
	queue.current_task = kernel_task;
	log_debug("Probably inited the scheduler");
	initialized = true;
}

/// Returns NULL if invalid next task
/// Sets the current task
struct task* scheduler_pick_next()
{
	if (queue.list == NULL) return NULL;
	// log_debug("Current task PID: %d", queue.current_task->PID);
	struct list* next = queue.current_task->list.next;
	queue.current_task = list_entry(next, struct task, list);
	// log_debug("Current task PID after iteration: %d", queue.current_task->PID);
	return queue.current_task;
}
