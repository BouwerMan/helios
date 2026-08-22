/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * kernel/spinlock.h - Test-and-set spinlocks for x86-64.
 *
 * See spinlock(9) for semantics, variant selection, and lock ordering
 * rules.
 */
#pragma once

#include "kernel/compiler_attributes.h"
#include "kernel/helios.h"

typedef struct {
	int val;
} spinlock_t;

/* Static initializer: `static spinlock_t my_lock = SPINLOCK_INIT;` */
#define SPINLOCK_INIT { 0 }

/* Declare and initialize a spinlock at file scope in one line. */
#define DEFINE_SPINLOCK(name) spinlock_t name = SPINLOCK_INIT

/**
 * @brief Initializes a spinlock to the unlocked state.
 *
 * @param lock Pointer to the spinlock to initialize.
 */
static inline void spin_init(spinlock_t* lock)
{
	__atomic_store_n(&lock->val, 0, __ATOMIC_RELAXED);
}

static inline void __spinlock_raw_acquire(spinlock_t* lock)
{
	while (__atomic_exchange_n(&lock->val, 1, __ATOMIC_ACQUIRE)) {
		while (__atomic_load_n(&lock->val, __ATOMIC_RELAXED))
			__builtin_ia32_pause();
	}
}

static inline void __spinlock_raw_release(spinlock_t* lock)
{
	__atomic_store_n(&lock->val, 0, __ATOMIC_RELEASE);
}

/**
 * @brief Acquires a spinlock. The interrupt state does not change.
 *
 * @param lock Pointer to the spinlock to acquire.
 */
static inline void spin_lock(spinlock_t* lock)
{
	__spinlock_raw_acquire(lock);
}

/**
 * @brief Releases a spinlock. The interrupt state does not change.
 *
 * @param lock Pointer to the spinlock to release.
 */
static inline void spin_unlock(spinlock_t* lock)
{
	__spinlock_raw_release(lock);
}

/**
 * @brief Makes one attempt to acquire a spinlock. Does not spin.
 *
 * @param lock Pointer to the spinlock to acquire.
 *
 * @return True if the spinlock was acquired. False if it was already held.
 */
static inline bool spin_trylock(spinlock_t* lock)
{
	return __atomic_exchange_n(&lock->val, 1, __ATOMIC_ACQUIRE) == 0;
}

/**
 * @brief Disables interrupts, then acquires a spinlock.
 *
 * @param lock Pointer to the spinlock to acquire.
 */
static inline void spin_lock_irq(spinlock_t* lock)
{
	__asm__ volatile("cli" : : : "memory");
	__spinlock_raw_acquire(lock);
}

/**
 * @brief Releases a spinlock, then enables interrupts.
 *
 * @param lock Pointer to the spinlock to release.
 */
static inline void spin_unlock_irq(spinlock_t* lock)
{
	__spinlock_raw_release(lock);
	__asm__ volatile("sti" : : : "memory");
}

/* EFLAGS.IF: set when maskable interrupts are enabled. */
static constexpr ulong EFLAGS_IF = 1UL << 9;

/**
 * @brief Saves the interrupt state, disables interrupts, then acquires
 * a spinlock.
 *
 * @param lock Pointer to the spinlock to acquire.
 *
 * @return The interrupt state to restore on unlock.
 */
static inline ulong spin_lock_irqsave(spinlock_t* lock)
{
	ulong flags;

	/* PUSHF pushes 8 bytes in long mode, matching the width of ulong. */
	__asm__ volatile("pushf; pop %0; cli" : "=r"(flags) : : "memory");
	__spinlock_raw_acquire(lock);
	return flags;
}

/**
 * @brief Releases a spinlock and restores the previous interrupt state.
 *
 * @param lock Pointer to the spinlock to release.
 * @param flags The value returned by the matching spin_lock_irqsave().
 */
static inline void spin_unlock_irqrestore(spinlock_t* lock, ulong flags)
{
	__spinlock_raw_release(lock);

	if (likely(flags & EFLAGS_IF)) __asm__ volatile("sti" : : : "memory");
}

typedef struct {
	spinlock_t* lock;
	ulong flags;
} spinlock_guard_t;

static inline spinlock_guard_t __spin_guard_init(spinlock_t* lock)
{
	return (spinlock_guard_t) { .lock = lock, .flags = spin_lock_irqsave(lock) };
}

static inline void __spin_guard_fini(spinlock_guard_t* guard)
{
	spin_unlock_irqrestore(guard->lock, guard->flags);
}

/**
 * @brief Holds a spinlock until the end of the current block.
 *
 * @param lockp Pointer to the spinlock to acquire.
 */
#define spin_guard(lockp) spinlock_guard_t __UNIQUE_ID(g) __cleanup(__spin_guard_fini) = __spin_guard_init(lockp)

/**
 * @brief Holds a spinlock for the statement that follows.
 *
 * @param lockp Pointer to the spinlock to acquire.
 *
 * This macro expands to a loop. See spinlock(9) for a warning about
 * break and continue.
 */
#define scoped_spin_guard(lockp) __scoped_spin_guard(lockp, __COUNTER__)

#define __scoped_spin_guard(lockp, id)                                                                            \
	for (spinlock_guard_t __CONCAT(__uid_guard_, id) __cleanup(__spin_guard_fini) = __spin_guard_init(lockp), \
							 *__CONCAT(__uid_done_, id) = nullptr;                    \
	     !__CONCAT(__uid_done_, id);                                                                          \
	     __CONCAT(__uid_done_, id) = (void*)1)
