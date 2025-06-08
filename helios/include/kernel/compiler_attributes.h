/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

// Always inline, even if the compiler thinks otherwise
#ifndef __always_inline
#define __always_inline inline __attribute__((always_inline))
#endif

// Never inline
#ifndef __noinline
#define __noinline __attribute__((noinline))
#endif

// Mark function or variable as unused to avoid warnings
#ifndef __unused
#define __unused __attribute__((unused))
#endif

// Mark function that does not return (e.g. panic, abort)
#ifndef __noreturn
#define __noreturn __attribute__((noreturn))
#endif

// Warn if result is unused
#ifndef __warn_unused_result
#define __warn_unused_result __attribute__((warn_unused_result))
#endif

/**
 * Function is pure (no side effects except return value)
 * Means that the function has no side effects and the value returned
 * depends on the arguments and the state of global variables.
 */
#ifndef __pure
#define __pure __attribute__((pure))
#endif

/**
 * Function is const (no side effects at all)
 * Means that the return value is solely a function of the arguments,
 * and if any of the arguments are pointers, then the pointers must not be dereferenced.
 */
#ifndef __constfunc
#define __constfunc __attribute__((const))
#endif

// Used for fallthrough annotations in switch statements (C++17-style)
#ifndef __fallthrough
#define __fallthrough __attribute__((fallthrough))
#endif

// Align a struct or variable
#ifndef __aligned
#define __aligned(x) __attribute__((aligned(x)))
#endif

// Used for placing symbols in specific linker sections
#ifndef __section
#define __section(x) __attribute__((section(x)))
#endif

#ifndef __packed
#define __packed __attribute__((packed))
#endif
