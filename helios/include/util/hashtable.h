/**
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2025 Dylan Parks
 *
 * This file is a derivative work based on the Linux kernel file:
 * include/linux/hashtable.h
 * include/linux/hash.h
 *
 * The original file from the Linux kernel is licensed under GPL-2.0
 * (SPDX-License-Identifier: GPL-2.0) and is copyrighted by the
 * Linux kernel contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once

#include <kernel/types.h>

/*
 * This hash multiplies the input by a large odd number and takes the
 * high bits.  Since multiplication propagates changes to the most
 * significant end only, it is essential that the high bits of the
 * product be used for the hash value.
 *
 * Chuck Lever verified the effectiveness of this technique:
 * http://www.citi.umich.edu/techreports/reports/citi-tr-00-1.pdf
 *
 * Although a random odd number will do, it turns out that the golden
 * ratio phi = (sqrt(5)-1)/2, or its negative, has particularly nice
 * properties.  (See Knuth vol 3, section 6.4, exercise 9.)
 *
 * These are the negative, (1 - phi) = phi**2 = (3 - sqrt(5))/2,
 * which is very slightly easier to multiply by and makes no
 * difference to the hash distribution.
 */
constexpr u32 GOLDEN_RATIO_32 = 0x61C88647;
constexpr u64 GOLDEN_RATIO_64 = 0x61C8864680B583EB;

/**
 * @brief Computes a 32-bit hash of a 32-bit value using multiplicative hashing.
 * @param val The 32-bit input value to hash.
 * @return A 32-bit hash value.
 */
[[gnu::const, gnu::always_inline]]
static inline u32 hash_32(u32 val, unsigned int bits)
{
	return (val * GOLDEN_RATIO_32) >> (32 - bits);
}

/**
 * @brief Computes a hash of a 64-bit value, scaled to a specified number of bits.
 * @param val  The 64-bit input value to hash.
 * @param bits The desired number of bits for the output hash value (1 to 64).
 * @return A hash value containing `bits` number of significant bits.
 */
[[gnu::const, gnu::always_inline]]
static inline u64 hash_64(u64 val, unsigned int bits)
{
	/* 64x64-bit multiply is efficient on all 64-bit processors */
	return (val * GOLDEN_RATIO_64) >> (64 - bits);
}
