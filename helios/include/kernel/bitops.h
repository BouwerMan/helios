/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <limits.h>

/**
 * How many bits wide is x?
 * Works with both types and expressions, handles arrays correctly
 */
#define BIT_WIDTH(x) (sizeof(typeof(x)) * CHAR_BIT)

/**
 * Get bit width of a specific type
 */
#define TYPE_BIT_WIDTH(type) (sizeof(type) * CHAR_BIT)

/**
 * Gets nth-bit
 */
#define BIT(n) (1ULL << (n))

/**
 * Macro to set pos bit
 */
#define SET_BIT(x, pos)                       \
	do {                                  \
		typeof(x)* _ptr = &(x);       \
		*_ptr |= (typeof(x))BIT(pos); \
	} while (0)

/**
 * Macro to clear pos bit
 */
#define CLEAR_BIT(x, pos)                        \
	do {                                     \
		typeof(x)* _ptr = &(x);          \
		*_ptr &= ~((typeof(x))BIT(pos)); \
	} while (0)

/**
 * Macro to check pos bit
 */
#define CHECK_BIT(x, pos) (!!((x) & (typeof(x))BIT(pos)))

/**
 * Macro to toggle pos bit
 */
#define TOGGLE_BIT(x, pos)                    \
	do {                                  \
		typeof(x)* _ptr &= &(x);      \
		*_ptr ^= (typeof(x))BIT(pos); \
	} while (0)

/**
 * Set multiple bits using a mask
 */
#define SET_BITS(x, mask)                 \
	do {                              \
		typeof(x)* _ptr &= &(x);  \
		*ptr |= (typeof(x))(mask) \
	} while (0)

/**
 * Clear multiple bits using a mask
 */
#define CLEAR_BITS(x, mask)                    \
	do {                                   \
		typeof(x)* _ptr = &(x);        \
		*_ptr &= ~((typeof(x))(mask)); \
	} while (0)

/**
 * Toggle multiple bits using a mask
 */
#define TOGGLE_BITS(x, mask)                \
	do {                                \
		typeof(x)* _ptr = &(x);     \
		*_ptr ^= (typeof(x))(mask); \
	} while (0)

/**
 * Create a bitmask with n bits set (starting from bit 0)
 * Example: BITMASK(3) = 0b111 = 7
 */
#define BITMASK(n) (BIT(n) - 1ULL)

/**
 * Create a bitmask for a range of bits [start, end] (inclusive)
 * Example: BITMASK_RANGE(2, 5) creates mask for bits 2,3,4,5
 */
#define BITMASK_RANGE(start, end) (BITMASK((end) - (start) + 1) << (start))

/**
 * Extract bits from position start to end (inclusive)
 */
#define EXTRACT_BITS(x, start, end) \
	(((x) >> (start)) & BITMASK((end) - (start) + 1))

/**
 * Insert value into specific bit range [start, end]
 * Clears the range first, then sets the new value
 */
#define INSERT_BITS(x, value, start, end)                                   \
	do {                                                                \
		typeof(x)* _ptr = &(x);                                     \
		typeof(x) _mask = BITMASK_RANGE(start, end);                \
		typeof(x) _val =                                            \
			((typeof(x))(value) & BITMASK((end) - (start) + 1)) \
			<< (start);                                         \
		*_ptr = (*_ptr & ~_mask) | _val;                            \
	} while (0)

/**
 * Count leading zeros (requires compiler builtin)
 */
#define CLZ(x) (__builtin_clzll((unsigned long long)(x)))

/**
 * Count trailing zeros
 */
#define CTZ(x) (__builtin_ctzll((unsigned long long)(x)))

/**
 * Count set bits (population count)
 */
#define POPCOUNT(x) (__builtin_popcountll((unsigned long long)(x)))

/**
 * Find first set bit (1-indexed, 0 if no bits set)
 */
#define FFS(x) (__builtin_ffsll((unsigned long long)(x)))

/**
 * Find last set bit (0-indexed, -1 if no bits set)
 */
#define FLS(x) (63 - CLZ(x))

#if 0
/**
 * Check if exactly one bit is set (power of 2 check)
 */
#define IS_POWER_OF_2(x)   ((x) && !((x) & ((x) - 1)))

/**
 * Round up to next power of 2
 */
#define ROUND_UP_POW2(x)   (1ULL << (64 - CLZ((x) - 1)))

/**
 * Round down to previous power of 2
 */
#define ROUND_DOWN_POW2(x) (1ULL << (63 - CLZ(x)))

/**
 * Byte swap operations
 */
#define BSWAP16(x)	   __builtin_bswap16(x)
#define BSWAP32(x)	   __builtin_bswap32(x)
#define BSWAP64(x)	   __builtin_bswap64(x)
#endif

/**
 * Find next set bit starting from position pos
 * Returns -1 if no more set bits found
 */
#define FIND_NEXT_BIT(x, pos)                       \
	({                                          \
		typeof(x) _x = (x) & ~BITMASK(pos); \
		_x ? CTZ(_x) : -1;                  \
	})

/**
 * Test and set/clear operations (return old bit value)
 */
#define TEST_AND_SET_BIT(x, pos)                    \
	({                                          \
		typeof(x) _old = CHECK_BIT(x, pos); \
		SET_BIT(x, pos);                    \
		_old;                               \
	})

#define TEST_AND_CLEAR_BIT(x, pos)                  \
	({                                          \
		typeof(x) _old = CHECK_BIT(x, pos); \
		CLEAR_BIT(x, pos);                  \
		_old;                               \
	})
