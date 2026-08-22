/**
 * @file kernel/work_queue.c
 *
 * Copyright (C) 2025  Dylan Parks
 *
 * This file is part of HeliOS
 *
 * HeliOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <uapi/helios/errno.h>

#include "kernel/spinlock.h"
#include "kernel/tasks/scheduler.h"
#include "kernel/work_queue.h"
#include "lib/list.h"
#include "lib/log.h"
#include "mm/kmalloc.h"

/**
 * @addtogroup kernel
 * @{
 */

static struct task* wq_task = nullptr;
static struct work_queue g_work_queue;

/**
 * @brief Removes and returns the next work item from the queue.
 *
 * @return A pointer to the work item if one is available, or nullptr if
 * the queue is empty.
 */
static struct work_item* take_from_queue()
{
	spin_guard(&g_work_queue.lock);
	struct work_item* item = nullptr;

	if (list_empty(&g_work_queue.queue)) {
		return item;
	}

	item = list_first_entry(&g_work_queue.queue, struct work_item, list);
	list_del(&item->list);

	return item;
}

/**
 * @brief Main entry point for worker threads.
 *
 * The loop follows the wait protocol of waitqueue(9), so no enqueued work
 * item is lost.
 */
static void worker_thread_entry(void)
{
	while (true) {
		waitqueue_prepare_wait(&g_work_queue.wq);

		struct work_item* work = take_from_queue();

		if (work) {
			waitqueue_cancel_wait(&g_work_queue.wq);
			work->func(work->data);
			kfree(work);
		} else {
			waitqueue_commit_sleep(&g_work_queue.wq);
		}
	}
}

/**
 * @brief Initializes the global work queue and worker thread.
 */
void work_queue_init()
{
	list_init(&g_work_queue.queue);
	spin_init(&g_work_queue.lock);
	waitqueue_init(&g_work_queue.wq);
	wq_task = kthread_create("Worker Queue task", (entry_func)worker_thread_entry);
	kthread_run(wq_task);
	log_debug("Initialized work queues");
}

/**
 * @brief Queues a work item for asynchronous execution.
 *
 * @param func Function to run when the work item is processed.
 * @param data Arbitrary data pointer to pass to the function.
 *
 * @return 0 on success, or -1 on memory allocation failure.
 */
int add_work_item(work_func_t func, void* data)
{
	if (!func) {
		log_error("Invalid function supplied (%p) by caller: %p", (void*)func, __builtin_return_address(0));
	}
	struct work_item* item = kmalloc(sizeof(struct work_item));
	if (!item) {
		return -ENOMEM;
	}

	item->func = func;
	item->data = data;

	scoped_spin_guard(&g_work_queue.lock)
	{
		list_add_tail(&g_work_queue.queue, &item->list);
	}

	waitqueue_wake_one(&g_work_queue.wq);

	return 0;
}

/** @} */
