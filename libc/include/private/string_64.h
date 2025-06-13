/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _PRIVATE__STRING_64_H
#define _PRIVATE__STRING_64_H 1
#pragma once

#define __need_size_t
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// These will be aliased in memset.c
extern void* memset64(uint64_t* s, uint64_t v, size_t n);
extern void* memset32(uint32_t* s, uint32_t v, size_t n);
extern void* memset16(uint16_t* s, uint16_t v, size_t n);
extern void* memset8(uint8_t* s, uint8_t v, size_t n);

#define __HAVE_ARCH_MEMSET8
static inline void* __arch_memset8(uint8_t* s, uint8_t v, size_t n)
{
	uint8_t* s0 = s;
	__asm__ volatile("rep stosb" : "+D"(s), "+c"(n) : "a"(v) : "memory");
	return s0;
}

#define __HAVE_ARCH_MEMSET16
static inline void* __arch_memset16(uint16_t* s, uint16_t v, size_t n)
{
	uint16_t* s0 = s;
	__asm__ volatile("rep stosw" : "+D"(s), "+c"(n) : "a"(v) : "memory");
	return s0;
}

#define __HAVE_ARCH_MEMSET32
static inline void* __arch_memset32(uint32_t* s, uint32_t v, size_t n)
{
	uint32_t* s0 = s;
	__asm__ volatile("rep stosl" : "+D"(s), "+c"(n) : "a"(v) : "memory");
	return s0;
}

#define __HAVE_ARCH_MEMSET64
static inline void* __arch_memset64(uint64_t* s, uint64_t v, size_t n)
{
	uint64_t* s0 = s;
	__asm__ volatile("rep stosq" : "+D"(s), "+c"(n) : "a"(v) : "memory");
	return s0;
}

#ifdef __cplusplus
}
#endif

#endif /* _PRIVATE__STRING_64_H */
