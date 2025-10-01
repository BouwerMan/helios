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

static struct task* wq_task = nullptr;
static struct work_queue g_work_queue;

/**
 * take_from_queue() - Remove and return the next work item from the queue
 * Return: Pointer to work item if available, nullptr if queue is empty
 */
static struct work_item* take_from_queue()
{
	unsigned long flags;
	spin_lock_irqsave(&g_work_queue.lock, &flags);
	struct work_item* item = nullptr;

	if (list_empty(&g_work_queue.queue)) {
		goto release;
	}

	item = list_first_entry(&g_work_queue.queue, struct work_item, list);
	list_del(&item->list);

release:
	spin_unlock_irqrestore(&g_work_queue.lock, flags);
	return item;
}

/**
 * worker_thread_entry() - Main entry point for worker threads
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
 * work_queue_init() - Initialize the global work queue and worker thread
 */
void work_queue_init()
{
	list_init(&g_work_queue.queue);
	spin_init(&g_work_queue.lock);
	wq_task = kthread_create("Worker Queue task",
				 (entry_func)worker_thread_entry);
	kthread_run(wq_task);
	log_debug("Initialized work queues");
}

/**
 * add_work_item() - Queue a work item for asynchronous execution
 * @func: Function to execute when the work item is processed
 * @data: Arbitrary data pointer to pass to the function
 * Return: 0 on success, -1 on memory allocation failure
 */
int add_work_item(work_func_t func, void* data)
{
	if (!func) {
		log_error("Invalid function supplied (%p) by caller: %p",
			  (void*)func,
			  __builtin_return_address(0));
	}
	struct work_item* item = kmalloc(sizeof(struct work_item));
	if (!item) {
		return -ENOMEM;
	}

	item->func = func;
	item->data = data;

	unsigned long flags;
	spin_lock_irqsave(&g_work_queue.lock, &flags);
	list_add_tail(&g_work_queue.queue, &item->list);
	spin_unlock_irqrestore(&g_work_queue.lock, flags);

	// TODO: make this a proper wake queue
	if (wq_task->state == BLOCKED) {
		task_wake(wq_task);
	}

	return 0;
}
