/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "kernel/types.h"

/**
 * @addtogroup lib
 * @{
 */

#define __STRING_H_CHECK_ALIGN(num, dest, src, size) ((num % size == 0) && (dest % size == 0) && (src % size == 0))

void* __memset(void* restrict d, int c, size_t n);
u16* __memset16(u16* restrict d, u16 v, size_t n);
u32* __memset32(u32* restrict d, u32 v, size_t n);
u64* __memset64(u64* restrict d, u64 v, size_t n);

#include "arch/lib/string.h"

#ifndef MEMSET_INLINE_MAX
#define MEMSET_INLINE_MAX 64	 // good starting point
#endif
#ifndef MEMSET16_INLINE_ELEMS
#define MEMSET16_INLINE_ELEMS 32 /* 32 * 2 = 64 bytes */
#endif
#ifndef MEMSET32_INLINE_ELEMS
#define MEMSET32_INLINE_ELEMS 16 /* 16 * 4 = 64 bytes */
#endif
#ifndef MEMSET64_INLINE_ELEMS
#define MEMSET64_INLINE_ELEMS 8	 /*  8 * 8 = 64 bytes */
#endif

/**
 * @brief Sets a block of memory to a byte value.
 *
 * @param d Destination buffer.
 * @param c Byte value to store. Uses the low 8 bits.
 * @param n Number of bytes to set.
 *
 * @return d.
 *
 * @note Does not sleep. Safe in any context.
 */
static inline void* memset(void* restrict d, int c, size_t n)
{
	if (__builtin_constant_p(n) && (n <= MEMSET_INLINE_MAX)) {
		return __builtin_memset(d, c, n);
	}
	return __memset(d, c, n);
}

/**
 * @brief Fills a buffer with a byte value.
 *
 * @param s Destination buffer.
 * @param v Byte value to store.
 * @param n Number of bytes to set.
 *
 * @return s.
 *
 * @note Does not sleep. Safe in any context.
 */
static inline u8* memset8(u8* restrict s, u8 v, size_t n)
{
	return (u8*)memset((void*)s, (int)v, n);
}

/**
 * @brief Fills a buffer with 16-bit elements of a value.
 *
 * @param s Destination buffer.
 * @param v 16-bit value to store.
 * @param n Number of 16-bit elements to set.
 *
 * @return s.
 *
 * @note Does not sleep. Safe in any context.
 */
static inline u16* memset16(u16* restrict s, u16 v, size_t n)
{
	if (__builtin_constant_p(n) && (n <= MEMSET16_INLINE_ELEMS)) {
		for (size_t i = 0; i < n; ++i)
			s[i] = v;
		return s;
	}
	return __memset16(s, v, n);
}

/**
 * @brief Fills a buffer with 32-bit elements of a value.
 *
 * @param s Destination buffer.
 * @param v 32-bit value to store.
 * @param n Number of 32-bit elements to set.
 *
 * @return s.
 *
 * @note Does not sleep. Safe in any context.
 */
static inline u32* memset32(u32* restrict s, u32 v, size_t n)
{
	if (__builtin_constant_p(n) && (n <= MEMSET32_INLINE_ELEMS)) {
		for (size_t i = 0; i < n; ++i)
			s[i] = v;
		return s;
	}
	return __memset32(s, v, n);
}

/**
 * @brief Fills a buffer with 64-bit elements of a value.
 *
 * @param s Destination buffer.
 * @param v 64-bit value to store.
 * @param n Number of 64-bit elements to set.
 *
 * @return s.
 *
 * @note Does not sleep. Safe in any context.
 */
static inline u64* memset64(u64* restrict s, u64 v, size_t n)
{
	if (__builtin_constant_p(n) && (n <= MEMSET64_INLINE_ELEMS)) {
		for (size_t i = 0; i < n; ++i)
			s[i] = v;
		return s;
	}
	return __memset64(s, v, n);
}

void* memcpy(void* restrict s1, const void* restrict s2, size_t n);
void* memmove(void* s1, const void* s2, size_t n);
char* strcpy(char* restrict s1, const char* restrict s2);
char* strncpy(char* restrict s1, const char* restrict s2, size_t n);
char* strdup(const char* s);
char* strndup(const char* s, size_t n);
char* strcat(char* restrict s1, const char* restrict s2);
char* strncat(char* restrict s1, const char* restrict s2, size_t n);

int memcmp(const void* s1, const void* s2, size_t n);

int strcmp(const char* s1, const char* s2);

int strncmp(const char* s1, const char* s2, size_t n);

[[gnu::pure]]
char* strchr(const char* s, int c);
char* strrchr(const char* s, int c);
char* strrnechr(const char* s, int c);

char* strtok(char* restrict s1, const char* restrict s2);

size_t strlen(const char*);

size_t strnlen_s(const char* s, size_t n);
size_t strnlen(const char* s, size_t n);
/** @} */
