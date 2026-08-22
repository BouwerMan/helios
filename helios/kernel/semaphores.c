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
 * @brief Initializes a counting semaphore.
 *
 * @param sem Target semaphore.
 * @param initial_count Initial number of available permits.
 *
 * @note Call this function before the first use of the semaphore. Do not
 * call it concurrently with other operations on the semaphore.
 */
void sem_init(semaphore_t* sem, int initial_count)
{
	spin_init(&sem->guard_lock);
	atomic_set(&sem->count, initial_count);
	waitqueue_init(&sem->waiters);
}

/**
 * @brief Acquires one permit from a semaphore. May block.
 *
 * @param sem Target semaphore.
 *
 * @note This function returns only after it acquires a permit. Call it
 * from process context only. Do not call it in IRQ or NMI context, or
 * while holding a spinlock.
 */
void sem_wait(semaphore_t* sem)
{
	while (true) {
		ulong flags = spin_lock_irqsave(&sem->guard_lock);

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
 * @brief Releases one permit and wakes a waiter if one is present.
 *
 * @param sem Target semaphore.
 *
 * @note This function is IRQ-safe. It wakes at most one waiter.
 */
void sem_signal(semaphore_t* sem)
{
	spin_guard(&sem->guard_lock);

#ifdef SEMAPHORE_DEBUG
	sem->owner = nullptr;
	sem->caller_addr = nullptr;
#endif

	atomic_inc(&sem->count);

	if (!waitqueue_empty(&sem->waiters)) {
		waitqueue_wake_one(&sem->waiters);
	}
}

/**
 * @brief Initializes a rwsem to the unlocked state.
 *
 * @param s Semaphore to initialize.
 *
 * Subsequent down and up operations enforce writer-preferred semantics.
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
 * @brief Acquires the rwsem for shared (reader) access.
 *
 * @param s Semaphore.
 *
 * @note This function may sleep. Do not call it in IRQ or atomic context.
 *
 * This function blocks while a writer is active or waiting, giving writers
 * preference. Multiple readers may hold the lock at once.
 */
void down_read(rwsem_t* s)
{
	ulong flags = spin_lock_irqsave(&s->guard);

	while (s->writer_active || s->writer_count > 0) {
		waitqueue_prepare_wait(&s->readers);
		spin_unlock_irqrestore(&s->guard, flags);
		waitqueue_commit_sleep(&s->readers);

		// Relock before rechecking condition
		flags = spin_lock_irqsave(&s->guard);
	}

	kassert(!s->writer_active, "Writer active in down_read");
	kassert(s->writer_count == 0, "Writers waiting in down_read");

	s->reader_count++;
	spin_unlock_irqrestore(&s->guard, flags);
}

/**
 * @brief Releases a shared (reader) hold on the rwsem.
 *
 * @param s Semaphore.
 *
 * @note This function does not sleep.
 *
 * If the reader count reaches zero and a writer is queued, this function
 * wakes one writer.
 */
void up_read(rwsem_t* s)
{
	spin_guard(&s->guard);

	s->reader_count--;
	if (s->reader_count == 0 && s->writer_count > 0) {
		waitqueue_wake_one(&s->writers);
	}
}

/**
 * @brief Acquires the rwsem for exclusive (writer) access.
 *
 * @param s Semaphore.
 *
 * @note This function may sleep. Do not call it in IRQ or atomic context.
 *
 * This function blocks until no readers are active and no writer holds the
 * lock. Writers take preference over new readers.
 */
void down_write(rwsem_t* s)
{
	ulong flags = spin_lock_irqsave(&s->guard);

	while (s->writer_active || s->reader_count > 0) {
		s->writer_count++;
		waitqueue_prepare_wait(&s->writers);
		spin_unlock_irqrestore(&s->guard, flags);
		waitqueue_commit_sleep(&s->writers);

		// Relock before rechecking condition
		flags = spin_lock_irqsave(&s->guard);
		s->writer_count--;
	}

	s->writer_active = true;
	spin_unlock_irqrestore(&s->guard, flags);
}

/**
 * @brief Releases an exclusive (writer) hold on the rwsem.
 *
 * @param s Semaphore.
 *
 * @note This function does not sleep.
 *
 * If a writer is waiting, this function wakes one writer. Otherwise, it
 * wakes all waiting readers.
 */
void up_write(rwsem_t* s)
{
	spin_guard(&s->guard);

	s->writer_active = false;

	if (s->writer_count > 0) {
		waitqueue_wake_one(&s->writers);
	} else if (s->reader_count == 0 && !waitqueue_empty(&s->readers)) {
		waitqueue_wake_all(&s->readers);
	}
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
