/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <arch/atomic.h>
#include <kernel/spinlock.h>
#include <kernel/tasks/scheduler.h>

typedef struct semaphore_s {
	atomic_t count;
	struct waitqueue waiters;
	spinlock_t guard_lock;
#ifdef SEMAPHORE_DEBUG
	struct task* owner;
	void* caller_addr;
#endif
} semaphore_t;

/**
 * sem_init - Initializes a semaphore to a clean and unlocked state.
 */
void sem_init(semaphore_t* sem, int initial_count);
void sem_wait(semaphore_t* sem);
void sem_signal(semaphore_t* sem);
