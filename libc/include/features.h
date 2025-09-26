/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _FEATURES_H
#define _FEATURES_H 1
#pragma once

#if (__STDC_VERSION__ >= 202311L)
#define __USE_C23
#endif

/**
 * Means that the return value is solely a function of the arguments,
 * and if any of the arguments are pointers, then the pointers must not be dereferenced.
 */
#define __attribute_const __attribute__((const))

/**
* Means that the function has no side effects and the value returned
* depends on the arguments and the state of global variables.
*/
#define __pure __attribute__((pure))

#define __noreturn __attribute__((noreturn))

#define __restrict __restrict__

#define __warn_unused_result __attribute__((warn_unused_result))

// Memory allocation attributes
#ifndef __alloc_size
#define __alloc_size(...) __attribute__((alloc_size(__VA_ARGS__)))
#endif

#ifndef __malloc_like
#define __malloc_like __attribute__((malloc))
#endif

#ifndef __nothrow
#define __nothrow __attribute__((nothrow))
#endif

#ifndef __nonnull
#define __nonnull(...) __attribute__((nonnull(__VA_ARGS__)))
#endif

#ifdef __GNUC__
#define __unimplemented(msg) __attribute__((error("Unimplemented: " msg)))
#else
#define __unimplemented(msg) /* Falls back to link error */
#endif

#endif			     /* _FEATURES_H */
