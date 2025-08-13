#include <kernel/spinlock.h>
#include <kernel/tasks/scheduler.h>
#include <kernel/work_queue.h>
#include <stdlib.h>
#include <util/list.h>
#include <util/log.h>

static struct task* wq_task = nullptr;
static struct work_queue g_work_queue;

int add_work_item(work_func_t func, void* data)
{
	struct work_item* item = kmalloc(sizeof(struct work_item));
	if (!item) {
		return -1;
	}

	item->func = func;
	item->data = data;

	spinlock_acquire(&g_work_queue.lock);
	list_add(&g_work_queue.queue, &item->list);
	spinlock_release(&g_work_queue.lock);

	// log_debug(
	// 	"Added work item with func %p and data %p", (void*)func, data);
	return 0;
}

struct work_item* take_from_queue()
{
	spinlock_acquire(&g_work_queue.lock);
	struct work_item* item = nullptr;

	if (list_empty(&g_work_queue.queue)) {
		goto release;
	}

	item = list_first_entry(&g_work_queue.queue, struct work_item, list);
	list_remove(&item->list);

release:
	spinlock_release(&g_work_queue.lock);
	return item;
}

void worker_thread_entry(void)
{
	while (true) {
		// Safely take the next work item from the queue
		struct work_item* work = take_from_queue();

		if (work) {
			// We have a job! Call the function.
			work->func(work->data);
			// The job is done, so we can free the work_item
			kfree(work);
		} else {
			// The queue is empty, so go to sleep until there's more work.
			yield_blocked();
		}
	}
}

void work_queue_init()
{
	list_init(&g_work_queue.queue);
	spinlock_init(&g_work_queue.lock);
	// TODO: Add task
	wq_task =
		new_task("Worker Queue task", (entry_func)worker_thread_entry);
	log_debug("Initialized work queues");
}
