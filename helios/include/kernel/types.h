/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdint.h>

// Unsigned types
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef unsigned char uchar;
typedef unsigned int uint;
typedef unsigned long ulong;

// Signed types
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

// Pointer-width types
typedef uintptr_t uptr;
typedef intptr_t iptr;

// Flags/bitmask type
typedef unsigned long flags_t;

typedef unsigned long size_t;
typedef long ssize_t;
typedef long off_t;

typedef unsigned long paddr_t;
typedef unsigned long vaddr_t;

typedef int pid_t;

typedef size_t pfn_t;
typedef long pgoff_t;

typedef struct {
	int counter;
} atomic_t;

typedef struct {
	long counter;
} atomic64_t;

struct list_head {
	struct list_head *next, *prev;
};

struct hlist_head {
	struct hlist_node* first;
};

struct hlist_node {
	struct hlist_node *next, **pprev;
};

/**
 * The following macros are a derivative work based on the Linux kernel file:
 * include/linux/compiler_types.h
 *
 * The original file from the Linux kernel is licensed under GPL-2.0
 * (SPDX-License-Identifier: GPL-2.0) and is copyrighted by the
 * Linux kernel contributors.
 */

// NOLINTBEGIN

/* Are two types/vars the same type (ignoring qualifiers)? */
#define __same_type(a, b) __builtin_types_compatible_p(typeof(a), typeof(b))

/*
  * __unqual_scalar_typeof(x) - Declare an unqualified scalar type, leaving
  *			       non-scalar types unchanged.
  */
/*
  * Prefer C11 _Generic for better compile-times and simpler code. Note: 'char'
  * is not type-compatible with 'signed char', and we define a separate case.
  */
#define __scalar_type_to_expr_cases(type) \
	unsigned type : (unsigned type)0, signed type : (signed type)0

#define __unqual_scalar_typeof(x)                              \
	typeof(_Generic((x),                                   \
		       char: (char)0,                          \
		       __scalar_type_to_expr_cases(char),      \
		       __scalar_type_to_expr_cases(short),     \
		       __scalar_type_to_expr_cases(int),       \
		       __scalar_type_to_expr_cases(long),      \
		       __scalar_type_to_expr_cases(long long), \
		       default: (x)))

/* Is this type a native word size -- useful for atomic operations */
#define __native_word(t)                                            \
	(sizeof(t) == sizeof(char) || sizeof(t) == sizeof(short) || \
	 sizeof(t) == sizeof(int) || sizeof(t) == sizeof(long))

//NOLINTEND
