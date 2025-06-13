/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

// 0 means unlocked
// 1 means locked
typedef volatile int spinlock_t;

/**
 * @brief Initializes a spinlock to the unlocked state.
 *
 * @param lock Pointer to the spinlock to initialize.
 */
static inline void spinlock_init(spinlock_t* lock)
{
	__atomic_store_n(lock, 0, __ATOMIC_RELAXED);
}

/**
 * @brief Acquires a spinlock by busy-waiting until it becomes available.
 *
 * Uses an atomic exchange and pause loop for efficient CPU spinning.
 *
 * @param lock Pointer to the spinlock to acquire.
 */
static inline void spinlock_acquire(spinlock_t* lock)
{
	// Passive spin (read-only)
	while (__atomic_exchange_n(lock, 1, __ATOMIC_ACQUIRE)) {
		while (__atomic_load_n(lock, __ATOMIC_RELAXED))
			__builtin_ia32_pause(); // hint to CPU: we're just spinning
	}
}

/**
 * @brief Releases a previously acquired spinlock.
 *
 * @param lock Pointer to the spinlock to release.
 */
static inline void spinlock_release(spinlock_t* lock)
{
	__atomic_store_n(lock, 0, __ATOMIC_RELEASE);
}
