/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <kernel/helios.h>

/*
 * Atomic operations on the atomic_t type
 */

/**
 * @brief Reads the value of an atomic variable.
 *
 * @param v Pointer to the atomic variable.
 *
 * @return The current value of the variable.
 */
static inline int atomic_read(const atomic_t* v)
{
	return __READ_ONCE((v)->counter);
}

/**
 * @brief Sets the value of an atomic variable.
 *
 * @param v Pointer to the atomic variable.
 * @param i Value to set.
 */
static inline void atomic_set(atomic_t* v, int i)
{
	__WRITE_ONCE(v->counter, i);
}

/**
 * @brief Adds an integer to an atomic variable.
 *
 * @param i Value to add.
 * @param v Pointer to the atomic variable.
 */
static inline void atomic_add(int i, atomic_t* v)
{
	__asm__ volatile("addl %1,%0" : "+m"(v->counter) : "ir"(i));
}

/**
 * @brief Subtracts an integer from an atomic variable.
 *
 * @param i Value to subtract.
 * @param v Pointer to the atomic variable.
 */
static inline void atomic_sub(int i, atomic_t* v)
{
	__asm__ volatile("subl %1,%0" : "+m"(v->counter) : "ir"(i));
}

/**
 * @brief Subtracts an integer from an atomic variable and tests the result.
 *
 * @param i Value to subtract.
 * @param v Pointer to the atomic variable.
 *
 * @return True if the result is zero. False otherwise.
 */
static inline int atomic_sub_and_test(int i, atomic_t* v)
{
	unsigned char c;

	__asm__ volatile("subl %2,%0; sete %1" : "+m"(v->counter), "=qm"(c) : "ir"(i) : "memory");
	return c;
}

/**
 * @brief Increments an atomic variable by 1.
 *
 * @param v Pointer to the atomic variable.
 */
static inline void atomic_inc(atomic_t* v)
{
	__asm__ volatile("incl %0" : "+m"(v->counter));
}

/**
 * @brief Decrements an atomic variable by 1.
 *
 * @param v Pointer to the atomic variable.
 */
static inline void atomic_dec(atomic_t* v)
{
	__asm__ volatile("decl %0" : "+m"(v->counter));
}

/*
 * Atomic operations on raw integers.
 */

/**
 * @brief Atomically sets a bit if it was clear.
 *
 * @param addr Pointer to the word that contains the bit.
 * @param bit Bit index, 0 to 63 for a 64-bit word.
 *
 * @return True if the bit changed from 0 to 1. False if it was already set.
 *
 * @note Uses 'lock' for full acquire and release ordering on x86.
 */
static inline bool try_set_bit(volatile unsigned long* addr, unsigned bit)
{
	unsigned char won;
	__asm__ __volatile__("lock bts %2, %1\n\t" // CF := old_bit; set bit
			     "setnc %0"		   // won = (CF == 0)
			     : "=q"(won), "+m"(*addr)
			     : "r"(bit)
			     : "cc", "memory");
	return won;
}

// Returns true iff we transitioned 0->1 (you "won").
static inline bool try_set_flag_mask(volatile unsigned long* addr, unsigned long mask)
{
	unsigned char won;
	__asm__ __volatile__("bsf %2, %%rcx\n\t"      // RCX = index of the set bit in mask
			     "lock bts %%rcx, %1\n\t" // CF := old_bit; set bit
			     "setnc %0"		      // won = (CF == 0)
			     : "=q"(won), "+m"(*addr)
			     : "r"(mask)
			     : "rcx", "cc", "memory");
	return won;
}

static inline bool try_clear_flag_mask(volatile unsigned long* addr, unsigned long mask)
{
	unsigned char cleared;
	__asm__ __volatile__("bsf %2, %%rcx\n\t"
			     "lock btr %%rcx, %1\n\t" // CF := old_bit; clear bit
			     "setc %0"		      // cleared = (CF == 1)
			     : "=q"(cleared), "+m"(*addr)
			     : "r"(mask)
			     : "rcx", "cc", "memory");
	return cleared;
}

static inline void clear_flag_mask(volatile unsigned long* addr, unsigned long mask)
{
	__asm__ __volatile__("bsf %1, %%rcx\n\t"      /* RCX = bit index from one-hot mask */
			     "lock btr %%rcx, %0\n\t" /* clear bit */
			     : "+m"(*addr)	      /* %0: RMW memory operand (output list!) */
			     : "r"(mask)	      /* %1: input mask */
			     : "rcx", "cc", "memory");
}

// TODO: Make READ_ONCE() macro
static inline bool flags_test_acquire(const volatile unsigned long* addr, unsigned long mask)
{
	unsigned long v = __atomic_load_n(addr, __ATOMIC_ACQUIRE);
	return (v & mask) != 0;
}
