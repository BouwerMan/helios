/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/**
 * @brief Optimized memory copy function for x86_64 architecture.
 *
 * This function uses the `rep movsb` instruction to perform a fast memory copy
 * operation. It is designed for scenarios where performance is critical.
 *
 * @param s1 Pointer to the destination buffer (must not be null).
 * @param s2 Pointer to the source buffer (must not be null).
 * @param n Number of bytes to copy from `s2` to `s1`.
 * @return Pointer to the destination buffer (`s1`).
 *
 * @note This function is architecture-specific and uses inline assembly.
 *       Ensure compatibility with the target platform before use.
 */
[[gnu::nonnull(1, 2), gnu::always_inline]]
static inline void* __fast_memcpy(void* restrict s1, const void* restrict s2, size_t n)
{
	uint8_t* s0 = (uint8_t*)s1;
	__asm__ volatile("rep movsb" : "+D"(s1), "+S"(s2), "+c"(n)::"memory");
	return s0;
}
