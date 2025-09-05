#include <kernel/semaphores.h>
#include <kernel/spinlock.h>
#include <kernel/tasks/scheduler.h>

/**
 * sem_init - Initializes a semaphore to a clean and unlocked state.
 */
void sem_init(semaphore_t* sem, int initial_count)
{
	spin_init(&sem->guard_lock);
	atomic_set(&sem->count, initial_count);
	waitqueue_init(&sem->waiters);
}

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
