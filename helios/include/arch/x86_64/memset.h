/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Efficiently sets a block of memory to a specified 64-bit value.
 *
 * This function uses the `rep stosq` assembly instruction to quickly set
 * a block of memory to the given 64-bit value. It is optimized for performance
 * and should only be used when the memory block is guaranteed to be aligned
 * to 64-bit boundaries.
 *
 * Intel's recommendation is to use `rep stosq` for setting
 * large (>2KiB) blocks of memory.
 *
 * @param s Pointer to the start of the memory block (must not be null).
 * @param v The 64-bit value to set in the memory block.
 * @param n The number of 64-bit values to set.
 * @return A pointer to the start of the memory block.
 */
[[gnu::nonnull(1), gnu::always_inline]]
static inline void* __fast_memset64(uint64_t* s, uint64_t v, size_t n)
{
	uint64_t* s0 = s;
	__asm__ volatile("rep stosq" : "+D"(s), "+c"(n) : "a"(v) : "memory");
	return s0;
}
