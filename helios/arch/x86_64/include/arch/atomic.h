/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <kernel/helios.h>

/*
 * Atomic operations on the atomic_t type
 */

/**
 * atomic_read - read atomic variable
 * @v: pointer of type atomic_t
 *
 * Atomically reads the value of @v.
 */
static inline int atomic_read(const atomic_t* v)
{
	return __READ_ONCE((v)->counter);
}

/**
 * atomic_set - set atomic variable
 * @v: pointer of type atomic_t
 * @i: required value
 *
 * Atomically sets the value of @v to @i.
 */
static inline void atomic_set(atomic_t* v, int i)
{
	__WRITE_ONCE(v->counter, i);
}

/**
 * atomic_add - add integer to atomic variable
 * @i: integer value to add
 * @v: pointer of type atomic_t
 *
 * Atomically adds @i to @v.
 */
static inline void atomic_add(int i, atomic_t* v)
{
	__asm__ volatile("addl %1,%0" : "+m"(v->counter) : "ir"(i));
}

/**
 * atomic_sub - subtract integer from atomic variable
 * @i: integer value to subtract
 * @v: pointer of type atomic_t
 *
 * Atomically subtracts @i from @v.
 */
static inline void atomic_sub(int i, atomic_t* v)
{
	__asm__ volatile("subl %1,%0" : "+m"(v->counter) : "ir"(i));
}

/**
 * atomic_sub_and_test - subtract value from variable and test result
 * @i: integer value to subtract
 * @v: pointer of type atomic_t
 *
 * Atomically subtracts @i from @v and returns
 * true if the result is zero, or false for all
 * other cases.
 */
static inline int atomic_sub_and_test(int i, atomic_t* v)
{
	unsigned char c;

	__asm__ volatile("subl %2,%0; sete %1"
			 : "+m"(v->counter), "=qm"(c)
			 : "ir"(i)
			 : "memory");
	return c;
}

/**
 * atomic_inc - increment atomic variable
 * @v: pointer of type atomic_t
 *
 * Atomically increments @v by 1.
 */
static inline void atomic_inc(atomic_t* v)
{
	__asm__ volatile("incl %0" : "+m"(v->counter));
}

/**
 * atomic_dec - decrement atomic variable
 * @v: pointer of type atomic_t
 *
 * Atomically decrements @v by 1.
 */
static inline void atomic_dec(atomic_t* v)
{
	__asm__ volatile("decl %0" : "+m"(v->counter));
}

/**
 * Atomic operations on raw integers
 */

/**
 * try_set_bit - atomically set a bit if it was clear
 * @addr: pointer to the word containing the bit
 * @bit:  bit index (0..63 for 64-bit words)
 *
 * Returns true if this call changed the bit 0->1 (you "won").
 * Returns false if the bit was already set by someone else.
 *
 * Ordering: 'lock' gives full acquire+release semantics on x86.
 * The "memory" clobber is a compiler barrier around the RMW.
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
static inline bool try_set_flag_mask(volatile unsigned long* addr,
				     unsigned long mask)
{
	unsigned char won;
	__asm__ __volatile__(
		"bsf %2, %%rcx\n\t"	 // RCX = index of the set bit in mask
		"lock bts %%rcx, %1\n\t" // CF := old_bit; set bit
		"setnc %0"		 // won = (CF == 0)
		: "=q"(won), "+m"(*addr)
		: "r"(mask)
		: "rcx", "cc", "memory");
	return won;
}

static inline bool try_clear_flag_mask(volatile unsigned long* addr,
				       unsigned long mask)
{
	unsigned char cleared;
	__asm__ __volatile__(
		"bsf %2, %%rcx\n\t"
		"lock btr %%rcx, %1\n\t" // CF := old_bit; clear bit
		"setc %0"		 // cleared = (CF == 1)
		: "=q"(cleared), "+m"(*addr)
		: "r"(mask)
		: "rcx", "cc", "memory");
	return cleared;
}

static inline void clear_flag_mask(volatile unsigned long* addr,
				   unsigned long mask)
{
	__asm__ __volatile__(
		"bsf %1, %%rcx\n\t"	 /* RCX = bit index from one-hot mask */
		"lock btr %%rcx, %0\n\t" /* clear bit */
		: "+m"(*addr) /* %0: RMW memory operand (output list!) */
		: "r"(mask)   /* %1: input mask */
		: "rcx", "cc", "memory");
}

// TODO: Make READ_ONCE() macro
static inline bool flags_test_acquire(const volatile unsigned long* addr,
				      unsigned long mask)
{
	unsigned long v = __atomic_load_n(addr, __ATOMIC_ACQUIRE);
	return (v & mask) != 0;
}
