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

void work_queue_init();

int add_work_item(work_func_t func, void* data);
