/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel/bitops.h"

/**
 * @brief Computes the integer base-2 logarithm of an unsigned long value.
 * @param v  Unsigned long value. Must be non-zero; behavior is undefined for zero.
 * @return   Zero-based index of the most significant set bit in v.
 */
static inline int ilog2(unsigned long v)
{
	return (int)BIT_WIDTH(v) - 1 - CLZ(v);
}

/**
 * @brief Rounds an unsigned long value up to the next highest power of two.
 * @param v  Unsigned long value.
 * @return   The smallest power of two greater than or equal to v.
 *           Returns 1 if v is zero.
 */
static inline unsigned long roundup_pow_of_two(unsigned long v)
{
	if (v == 0 || v == 1) return 1;
	return 1ULL << ((int)BIT_WIDTH(v) - CLZ(v - 1));
}

/**
 * @brief Rounds an unsigned long down to the nearest power of two.
 *        Returns 0 for zero input.
 */
static inline unsigned long rounddown_pow_of_two(unsigned long v)
{
	if (v == 0) return 0;
	return 1UL << ((int)BIT_WIDTH(v) - 1 - CLZ(v));
}

/**
 * @brief Checks if an unsigned long value is a power of two.
 * @param n  Unsigned long value.
 * @return   true if n is a power of two (and non-zero), false otherwise.
 */
static inline bool is_pow_of_two(unsigned long n)
{
	return (n != 0 && ((n & (n - 1)) == 0));
}
