#include <kernel/spinlock.h>
#include <kernel/tasks/scheduler.h>
#include <kernel/work_queue.h>
#include <stdlib.h>
#include <util/list.h>
#include <util/log.h>

/*******************************************************************************
 * Global Variable Definitions
 *******************************************************************************/

static struct task* wq_task = nullptr;
static struct work_queue g_work_queue;

/*******************************************************************************
 * Private Function Prototypes
 *******************************************************************************/

/**
 * worker_thread_entry - Main entry point for worker threads
 */
static void worker_thread_entry(void);

/**
 * take_from_queue - Remove and return the next work item from the queue
 *
 * Return: Pointer to work item if available, nullptr if queue is empty
 */
static struct work_item* take_from_queue();

/*******************************************************************************
 * Public Function Definitions
 *******************************************************************************/

/**
 * work_queue_init - Initialize the global work queue subsystem
 *
 * Sets up the work queue infrastructure by initializing the queue list,
 * spinlock, and creating the worker thread. This must be called during
 * kernel initialization before any work items can be queued.
 */
void work_queue_init()
{
	list_init(&g_work_queue.queue);
	spinlock_init(&g_work_queue.lock);
	wq_task = kthread_create("Worker Queue task",
				 (entry_func)worker_thread_entry);
	kthread_run(wq_task);
	log_debug("Initialized work queues");
}

/**
 * add_work_item - Queue a work item for asynchronous execution
 * @func: Function to execute when the work item is processed
 * @data: Arbitrary data pointer to pass to the function
 *
 * Allocates and queues a work item that will be executed asynchronously
 * by the worker thread. The work item is added to the tail of the queue
 * and the worker thread is woken if it's currently blocked. Memory
 * allocation failures will cause the work item to be dropped.
 *
 * Return: 0 on success, -1 on memory allocation failure
 */
int add_work_item(work_func_t func, void* data)
{
	struct work_item* item = kmalloc(sizeof(struct work_item));
	if (!item) {
		return -1;
	}

	item->func = func;
	item->data = data;

	spinlock_acquire(&g_work_queue.lock);
	list_add_tail(&g_work_queue.queue, &item->list);
	spinlock_release(&g_work_queue.lock);

	// TODO: make this a proper wake queue
	if (wq_task->state == BLOCKED) {
		wq_task->state = READY;
	}

	return 0;
}

/*******************************************************************************
 * Private Function Definitions
 *******************************************************************************/

/**
 * worker_thread_entry - Main entry point for worker threads
 *
 * The main loop for worker threads that process items from the work queue.
 * Each worker thread continuously takes work items from the queue and
 * executes them. When no work is available, the thread yields and blocks
 * until new work arrives. Work items are automatically freed after execution.
 *
 * This function runs indefinitely and never returns.
 */
static void worker_thread_entry(void)
{
	while (true) {
		struct work_item* work = take_from_queue();

		if (work) {
			work->func(work->data);
			kfree(work);
		} else {
			yield_blocked();
		}
	}
}

/**
 * take_from_queue - Remove and return the next work item from the queue
 *
 * Atomically removes the first work item from the head of the work queue.
 * If the queue is empty, returns nullptr. The caller is responsible for
 * freeing the returned work item after processing. This function is
 * thread-safe and protected by the work queue spinlock.
 *
 * Return: Pointer to work item if available, nullptr if queue is empty
 */
static struct work_item* take_from_queue()
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
