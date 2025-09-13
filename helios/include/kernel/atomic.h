/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <arch/atomic.h>

#define ATOMIC_INIT(i)	 { (i) }
#define ATOMIC64_INIT(i) { (i) }

#define barrier()   asm volatile("" ::: "memory")
#define cpu_relax() __builtin_ia32_pause();

static inline long atomic64_fetch_add_relaxed(atomic64_t* v, long delta)
{
	return __atomic_fetch_add(&v->counter, delta, __ATOMIC_RELAXED);
}

static inline long atomic64_load_relaxed(const atomic64_t* v)
{
	return __atomic_load_n(&v->counter, __ATOMIC_RELAXED);
}

static inline void smp_store_release_u32(unsigned int* p, unsigned int v)
{
	__atomic_store_n(p, v, __ATOMIC_RELEASE);
}

static inline unsigned int smp_load_acquire_u32(const unsigned int* p)
{
	return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}

static inline void smp_mb(void)
{
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
}

static inline long atomic64_compare_and_swap(atomic64_t* v, long old, long new)
{
	return __atomic_compare_exchange_n(&v->counter,
					   &old,
					   new,
					   false,
					   __ATOMIC_SEQ_CST,
					   __ATOMIC_SEQ_CST);
}

/**
 * a64_cas_relaxed - 64-bit compare-and-swap with relaxed ordering
 * @v:   pointer to atomic64_t
 * @old: IN/OUT expected value; on failure, overwritten with the observed value
 * @new: desired value if *v == *old
 *
 * Returns:
 *   true  if the exchange took place (i.e., *v was equal to *old),
 *   false otherwise (and *@old is updated to the current *v).
 */
static inline bool a64_cas_relaxed(atomic64_t* v, long* old, long new)
{
	return __atomic_compare_exchange_n(&v->counter,
					   old,
					   new,
					   false,
					   __ATOMIC_RELAXED,
					   __ATOMIC_RELAXED);
}
