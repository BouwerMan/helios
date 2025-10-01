/**
 * @file kernel/semaphores.c
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

#include "kernel/semaphores.h"
#include "kernel/assert.h"
#include "kernel/spinlock.h"
#include "kernel/tasks/scheduler.h"

/**
 * sem_init() - Initialize a counting semaphore.
 * @sem: Target semaphore.
 * @initial_count: Initial number of available permits.
 * Return: None.
 * Context: Any. Must be called before first use and not concurrently with
 *          other operations on @sem.
 * Locks: None required; internal synchronization is established by init.
 */
void sem_init(semaphore_t* sem, int initial_count)
{
	spin_init(&sem->guard_lock);
	atomic_set(&sem->count, initial_count);
	waitqueue_init(&sem->waiters);
}

/**
 * sem_wait() - Acquire one permit from a semaphore (may block).
 * @sem: Target semaphore.
 * Return: None. Returns only after a permit is acquired.
 * Context: Process context only; may sleep. Not valid in IRQ, NMI, or while
 *          holding spinlocks or in other atomic sections.
 * Locks: No external locks required; wait/queueing is handled internally.
 */
void sem_wait(semaphore_t* sem)
{
	while (true) {
		unsigned long flags;
		spin_lock_irqsave(&sem->guard_lock, &flags);

		if (atomic_read(&sem->count) > 0) {
			// Permit is available, take it and go.
			atomic_dec(&sem->count);
			spin_unlock_irqrestore(&sem->guard_lock, flags);
#ifdef SEMAPHORE_DEBUG
			sem->owner = get_current_task();
			sem->caller_addr = __builtin_return_address(0);
#endif
			return;
		}
		waitqueue_prepare_wait(&sem->waiters);
		spin_unlock_irqrestore(&sem->guard_lock, flags);
		waitqueue_commit_sleep(&sem->waiters);
	}
}

/**
 * sem_signal() - Release one permit and wake a waiter if present.
 * @sem: Target semaphore.
 * Return: None.
 * Context: Any; IRQ-safe.
 * Locks: No external locks required; wakeup and bookkeeping are internal.
 * Notes: Wakes at most one waiter. In debug builds, clears ownership
 *        metadata before releasing the permit.
 */
void sem_signal(semaphore_t* sem)
{
	unsigned long flags;
	spin_lock_irqsave(&sem->guard_lock, &flags);

#ifdef SEMAPHORE_DEBUG
	sem->owner = nullptr;
	sem->caller_addr = nullptr;
#endif

	atomic_inc(&sem->count);

	if (!waitqueue_empty(&sem->waiters)) {
		waitqueue_wake_one(&sem->waiters);
	}

	spin_unlock_irqrestore(&sem->guard_lock, flags);
}

/**
 * rwsem_init() - Initialize an rwsem to the unlocked state
 * @s: semaphore to initialize
 *
 * Sets up wait queues and clears counters/flags. Subsequent down/up
 * operations enforce writer-preferred semantics.
 *
 * Return: void
 */
void rwsem_init(rwsem_t* s)
{
	spin_init(&s->guard);
	waitqueue_init(&s->readers);
	waitqueue_init(&s->writers);
	s->reader_count = 0;
	s->writer_count = 0;
	s->writer_active = false;
}

/**
 * down_read() - Acquire the rwsem for shared (reader) access
 * @s: semaphore
 *
 * Blocks if a writer is active or waiting, giving writers preference.
 * Multiple readers may hold the lock when no writer is active/queued.
 *
 * Context: May sleep; not in IRQ/atomic context.
 * Return: void
 */
void down_read(rwsem_t* s)
{
	unsigned long flags;
	spin_lock_irqsave(&s->guard, &flags);

	while (s->writer_active || s->writer_count > 0) {
		waitqueue_prepare_wait(&s->readers);
		spin_unlock_irqrestore(&s->guard, flags);
		waitqueue_commit_sleep(&s->readers);

		// Relock before rechecking condition
		spin_lock_irqsave(&s->guard, &flags);
	}

	kassert(!s->writer_active, "Writer active in down_read");
	kassert(s->writer_count == 0, "Writers waiting in down_read");

	s->reader_count++;
	spin_unlock_irqrestore(&s->guard, flags);
}

/**
 * up_read() - Release a shared (reader) hold on the rwsem
 * @s: semaphore
 *
 * Decrements the reader count; if it reaches zero and writers are queued,
 * wake one writer.
 *
 * Context: Does not sleep.
 * Return: void
 */
void up_read(rwsem_t* s)
{
	unsigned long flags;
	spin_lock_irqsave(&s->guard, &flags);

	s->reader_count--;
	if (s->reader_count == 0 && s->writer_count > 0) {
		waitqueue_wake_one(&s->writers);
	}

	spin_unlock_irqrestore(&s->guard, flags);
}

/**
 * down_write() - Acquire the rwsem for exclusive (writer) access
 * @s: semaphore
 *
 * Blocks until no readers are active and no writer holds the lock. On
 * success marks the writer as active; writers are preferred over new readers.
 *
 * Context: May sleep; not in IRQ/atomic context.
 * Return: void
 */
void down_write(rwsem_t* s)
{
	unsigned long flags;
	spin_lock_irqsave(&s->guard, &flags);

	while (s->writer_active || s->reader_count > 0) {
		s->writer_count++;
		waitqueue_prepare_wait(&s->writers);
		spin_unlock_irqrestore(&s->guard, flags);
		waitqueue_commit_sleep(&s->writers);

		// Relock before rechecking condition
		spin_lock_irqsave(&s->guard, &flags);
		s->writer_count--;
	}

	s->writer_active = true;
	spin_unlock_irqrestore(&s->guard, flags);
}

/**
 * up_write() - Release an exclusive (writer) hold on the rwsem
 * @s: semaphore
 *
 * Clears writer_active. If writers are waiting, wake one; otherwise wake
 * all readers to proceed in batch.
 *
 * Context: Does not sleep.
 * Return: void
 */
void up_write(rwsem_t* s)
{
	unsigned long flags;
	spin_lock_irqsave(&s->guard, &flags);

	s->writer_active = false;

	if (s->writer_count > 0) {
		waitqueue_wake_one(&s->writers);
	} else if (s->reader_count == 0 && !waitqueue_empty(&s->readers)) {
		waitqueue_wake_all(&s->readers);
	}

	spin_unlock_irqrestore(&s->guard, flags);
}

void downgrade_write(rwsem_t* s)
{
	(void)s;
	kunimpl("downgrade_write");
}
bool try_down_read(rwsem_t* s)
{
	(void)s;
	kunimpl("try_down_read");
}
bool try_down_write(rwsem_t* s)
{
	(void)s;
	kunimpl("try_down_write");
}
