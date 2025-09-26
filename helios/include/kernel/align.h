/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdint.h>

/**
 * Type-safe alignment macros with compile-time validation
 * Ensures align is power of 2 and handles type promotion correctly
 */

#ifndef __ALIGN_PANIC
#include "kernel/panic.h"
#define __ALIGN_PANIC(msg) panic(msg)
#endif

/* True for runtime expressions. Uses a temp of the same type as (a). */
#define __ALIGN_IS_POW2_RUNTIME(a)                          \
	({                                                  \
		typeof(a) __aa = (a);                       \
		(__aa > 0) && (((__aa & (__aa - 1)) == 0)); \
	})

/* Compile-time check: only instantiated when 'a' is an ICE. */
#define __ALIGN_CHECK_CT(a)                                                   \
	((void)__builtin_choose_expr(                                         \
		__builtin_constant_p(a),                                      \
		sizeof(char[((a) > 0 && (((a) & ((a) - 1)) == 0)) ? 1 : -1]), \
		0))

/* Constant-only API (for your *_CONST variants) */
#define __ALIGN_CHECK_CONST(a) \
	((void)sizeof(char[((a) > 0 && (((a) & ((a) - 1)) == 0)) ? 1 : -1]))

/* Hybrid check: compile-time when possible, runtime otherwise */
#define _ALIGN_CHECK(a)                                                      \
	do {                                                                 \
		__ALIGN_CHECK_CT(a);                                         \
		if (!__builtin_constant_p(a)) {                              \
			if (!__ALIGN_IS_POW2_RUNTIME(a))                     \
				__ALIGN_PANIC(                               \
					"align must be a power of two > 0"); \
		}                                                            \
	} while (0)

/**
 * Align value up to nearest multiple of align (align must be power of 2)
 * Type-safe version that preserves the type of x
 */
#define ALIGN_UP(x, align)                    \
	({                                    \
		_ALIGN_CHECK(align);          \
		typeof(x) _x = (x);           \
		typeof(x) _align = (align);   \
		typeof(x) _mask = _align - 1; \
		(_x + _mask) & ~_mask;        \
	})

/**
 * Align value down to nearest multiple of align (align must be power of 2)
 * Type-safe version that preserves the type of x
 */
#define ALIGN_DOWN(x, align)                  \
	({                                    \
		_ALIGN_CHECK(align);          \
		typeof(x) _x = (x);           \
		typeof(x) _align = (align);   \
		typeof(x) _mask = _align - 1; \
		_x & ~_mask;                  \
	})

/**
 * Safer versions that work with constant alignments only
 * Better for performance-critical code where alignment is known at compile time
 */
#define ALIGN_UP_CONST(x, align)                            \
	(__builtin_constant_p(align) ?                      \
		 (__ALIGN_CHECK_CONST(align),               \
		  (typeof(x))(((x) + (align) - 1) &         \
			      ~((typeof(x))(align) - 1))) : \
		 ALIGN_UP(x, align))

#define ALIGN_DOWN_CONST(x, align)                                \
	(__builtin_constant_p(align) ?                            \
		 (__ALIGN_CHECK_CONST(align),                     \
		  (typeof(x))((x) & ~((typeof(x))(align) - 1))) : \
		 ALIGN_DOWN(x, align))

/**
 * Pointer-specific alignment macros
 * Handles pointer arithmetic correctly
 */
#define ALIGN_PTR_UP(ptr, align) \
	((typeof(ptr))ALIGN_UP((uintptr_t)(ptr), (align)))

#define ALIGN_PTR_DOWN(ptr, align) \
	((typeof(ptr))ALIGN_DOWN((uintptr_t)(ptr), (align)))

/**
 * Check if value is aligned to boundary
 * Type-safe version
 */
#define IS_ALIGNED(x, align)                \
	({                                  \
		_ALIGN_CHECK(align);        \
		typeof(x) _x = (x);         \
		typeof(x) _align = (align); \
		(_x & (_align - 1)) == 0;   \
	})
