/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "kernel/helios.h"

// 0 means unlocked
// 1 means locked
typedef volatile int spinlock_t;

static constexpr int SPINLOCK_INIT = 0;

static inline void spin_init(spinlock_t* lock)
{
	__atomic_store_n(lock, 0, __ATOMIC_RELAXED);
}

static inline void __spinlock_raw_acquire(spinlock_t* lock)
{
	while (__atomic_exchange_n(lock, 1, __ATOMIC_ACQUIRE)) {
		while (__atomic_load_n(lock, __ATOMIC_RELAXED))
			__builtin_ia32_pause(); // hint to CPU: we're just spinning
	}
}

static inline void __spinlock_raw_release(spinlock_t* lock)
{
	__atomic_store_n(lock, 0, __ATOMIC_RELEASE);
}

/**
 * spin_lock_irqsave() - Disables interrupts then acquires a spinlock.
 * @lock: Pointer to the spinlock to acquire.
 * @flags: A pointer to an unsigned long where the interrupt state will be saved.
 */
static inline void spin_lock_irqsave(spinlock_t* lock, unsigned long* flags)
{
	__asm__ volatile("pushf; pop %0; cli" : "=r"(*flags) : : "memory");
	__spinlock_raw_acquire(lock);
}

/**
 * spin_unlock_irqrestore() - Releases a spinlock and restores the previous interrupt state.
 * @lock: Pointer to the spinlock to release.
 * @flags: The saved interrupt state to restore.
 */
static inline void spin_unlock_irqrestore(spinlock_t* lock, unsigned long flags)
{
	__spinlock_raw_release(lock);
	static constexpr int EFLAGS_IF_BIT = 9;
	static constexpr int EFLAGS_IF = (1UL << EFLAGS_IF_BIT);

	// Only restore interrupts if they were enabled in the saved flags
	if (likely(flags & EFLAGS_IF)) {
		__asm__ volatile("sti" : : : "memory");
	}
}

/**
 * spin_lock_irq() - Unconditionally disables interrupts then acquires a spinlock.
 * @lock: Pointer to the spinlock to acquire.
 */
static inline void spin_lock_irq(spinlock_t* lock)
{
	__asm__ volatile("cli");
	__spinlock_raw_acquire(lock);
}

/**
 * spin_unlock_irq() - Releases a spinlock and unconditionally enables interrupts.
 * @lock: Pointer to the spinlock to release.
 */
static inline void spin_unlock_irq(spinlock_t* lock)
{
	__spinlock_raw_release(lock);
	__asm__ volatile("sti");
}

/**
 * spin_lock() - Acquire a spinlock.
 * @lock: Pointer to the spinlock to acquire.
 *
 * This function will block (spin) until the lock is acquired.
 * It should only be used if in interrupt context or if there is no intention
 * to release the lock.
 */
static inline void spin_lock(spinlock_t* lock)
{
	__spinlock_raw_acquire(lock);
}

/**
 * spin_unlock() - Release a spinlock.
 * @lock: Pointer to the spinlock to release.
 *
 * This function should only be used if in interrupt context or if there is no intention
 * to release the lock.
 */
static inline void spin_unlock(spinlock_t* lock)
{
	__spinlock_raw_release(lock);
}
