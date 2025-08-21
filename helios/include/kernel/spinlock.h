/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <kernel/helios.h>

// 0 means unlocked
// 1 means locked
typedef volatile int spinlock_t;

/**
 * @brief Initializes a spinlock to the unlocked state.
 * @param lock Pointer to the spinlock to initialize.
 */
static inline void spin_init(spinlock_t* lock)
{
	__atomic_store_n(lock, 0, __ATOMIC_RELAXED);
}

[[deprecated]]
static inline void spinlock_init(spinlock_t* lock)
{
	spin_init(lock);
}

static inline void __spinlock_raw_acquire(spinlock_t* lock)
{
	// Passive spin (read-only)
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
 * @brief Acquires a spinlock by busy-waiting until it becomes available.
 * @param lock Pointer to the spinlock to acquire.
 */
[[deprecated]]
static inline void spinlock_acquire(spinlock_t* lock)
{
	__spinlock_raw_acquire(lock);
}

/**
 * @brief Releases a previously acquired spinlock.
 * @param lock Pointer to the spinlock to release.
 */
[[deprecated]]
static inline void spinlock_release(spinlock_t* lock)
{
	__spinlock_raw_release(lock);
}

/**
 * @brief Saves the current interrupt state (RFLAGS) and disables interrupts.
 *
 * @return An unsigned long containing the RFLAGS value before disabling interrupts.
 */
static inline unsigned long spinlock_irqsave(void)
{
	unsigned long flags;
	__asm__ volatile("pushf; pop %0; cli" : "=r"(flags) : : "memory");
	return flags;
}

/**
 * @brief Restores a previously saved interrupt state (RFLAGS).
 *
 * @param flags The RFLAGS value to restore.
 */
static inline void spinlock_irqrestore(unsigned long flags)
{
	static constexpr int EFLAGS_IF_BIT = 9;
	static constexpr int EFLAGS_IF = (1UL << EFLAGS_IF_BIT);

	// Only restore interrupts if they were enabled in the saved flags
	if (likely(flags & EFLAGS_IF)) {
		__asm__ volatile("sti" : : : "memory");
	}
}

/**
 * @brief Acquires a spinlock and disables interrupts.
 *
 * This function first saves the current interrupt state, then disables interrupts,
 * and finally acquires the spinlock by busy-waiting.
 *
 * @param lock Pointer to the spinlock to acquire.
 * @param flags A pointer to an unsigned long where the interrupt state will be saved.
 */
static inline void spin_lock_irqsave(spinlock_t* lock, unsigned long* flags)
{
	*flags = spinlock_irqsave();
	__spinlock_raw_acquire(lock);
}

/**
 * @brief Releases a spinlock and restores the previous interrupt state.
 *
 * @param lock Pointer to the spinlock to release.
 * @param flags The saved interrupt state to restore.
 */
static inline void spin_unlock_irqrestore(spinlock_t* lock, unsigned long flags)
{
	__spinlock_raw_release(lock);
	spinlock_irqrestore(flags);
}

// TODO: Flesh these out with proper documentation and such, in the meantime
// just know that you probably shouldn't use these :)

// Should be used if in interrupt context
#define spin_lock(lock)	  spinlock_acquire(lock)
#define spin_unlock(lock) spinlock_release(lock)

static inline void spin_lock_irq(spinlock_t* lock)
{
	__asm__ volatile("cli");
	__spinlock_raw_acquire(lock);
}

static inline void spin_unlock_irq(spinlock_t* lock)
{
	__spinlock_raw_release(lock);
	__asm__ volatile("sti");
}
