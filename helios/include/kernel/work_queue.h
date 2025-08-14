/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <kernel/spinlock.h>
#include <kernel/types.h>

typedef void (*work_func_t)(void* data);

struct work_item {
	struct list_head list; // To link it into the queue
	work_func_t func;      // The function to call to perform the work
	void* data;	       // A pointer to any data the function needs
};

struct work_queue {
	struct list_head queue; // List of work items
	spinlock_t lock;	// Lock to protect the queue
};

/**
 * work_queue_init - Initialize the global work queue subsystem
 */
void work_queue_init();

/**
 * add_work_item - Queue a work item for asynchronous execution
 * @func: Function to execute when the work item is processed
 * @data: Arbitrary data pointer to pass to the function
 *
 * Return: 0 on success, -1 on memory allocation failure
 */
int add_work_item(work_func_t func, void* data);
