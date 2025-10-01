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

void sem_init(semaphore_t* sem, int initial_count);
void sem_wait(semaphore_t* sem);
void sem_signal(semaphore_t* sem);

/*
 * Allows multiple readers or a single writer to access a shared resource.
 * Provides priority to writers to prevent writer starvation.
 */
typedef struct rwsem {
	spinlock_t guard;	  /**< Spinlock protecting semaphore state */
	struct waitqueue readers; /**< Queue of waiting reader threads */
	struct waitqueue writers; /**< Queue of waiting writer threads */
	int reader_count;	  /**< Number of active readers */
	int writer_count;	  /**< Number of waiting writers */
	bool writer_active; /**< True if a writer currently holds the lock */
} rwsem_t;

void rwsem_init(rwsem_t* s);

void down_read(rwsem_t* s);  // may sleep
void up_read(rwsem_t* s);

void down_write(rwsem_t* s); // may sleep
void up_write(rwsem_t* s);

/*
 * Unimplemented:
 */

void downgrade_write(rwsem_t* s); // writer → reader, no gap
bool try_down_read(rwsem_t* s);
bool try_down_write(rwsem_t* s);
