/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "kernel/helios.h"
#include "kernel/panic.h"
#include "lib/log.h"

// Tooling helpers
#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

// Enable/disable policy:
//  - Disabled if NDEBUG or KASSERT_DISABLE is set
//  - Force-enable with KASSERT_ENABLE (even if NDEBUG is set)
#if !defined(KASSERT_DISABLE) && !defined(NDEBUG)
#define __KASSERT_ENABLED 1
#else
#define __KASSERT_ENABLED 0
#endif
#if defined(KASSERT_ENABLE)
#undef __KASSERT_ENABLED
#define __KASSERT_ENABLED 1
#endif

// Cold + noreturn helps code layout and optimization on the fail path.
[[gnu::cold, noreturn, maybe_unused]]
static void __kassert_fail_base(const char* expr,
				const char* file,
				int line,
				const char* func)
{
	set_log_mode(LOG_DIRECT);
	// Keep the core record structured and minimal; no allocation.
	log_error("Assertion failed: (%s)", expr);
	log_error("  at %s:%d in %s()", file, line, func);
	panic("Kernel assertion failed, halting system.");
	__builtin_unreachable();
}

// kassert(expr [, fmt, ...])
// - Evaluates expr exactly once.
// - In debug: logs the failure site and optional message, then panics.
// - In release (NDEBUG or KASSERT_DISABLE): compiles away to nothing.
#if __KASSERT_ENABLED
#define kassert(expr, ...)                                  \
	do {                                                \
		if (unlikely(!(expr))) {                    \
			__VA_OPT__(log_error(__VA_ARGS__);) \
			__kassert_fail_base(#expr,          \
					    __FILE__,       \
					    __LINE__,       \
					    __func__);      \
		}                                           \
	} while (0)
#else
#define kassert(expr, ...) ((void)0)
#endif

// kunreachable(): like libc's __builtin_unreachable(), but safe in debug.
#if __KASSERT_ENABLED
#define kunreachable() \
	__kassert_fail_base("unreachable", __FILE__, __LINE__, __func__)
#else
#if __has_builtin(__builtin_unreachable)
#define kunreachable() __builtin_unreachable()
#else
#define kunreachable() ((void)0)
#endif
#endif

// kassume(cond): treat 'cond' as a trusted invariant in release builds,
// while still asserting it in debug builds (useful for optimizer hints).
#if __KASSERT_ENABLED
#define kassume(cond) kassert(cond)
#else
#if __has_builtin(__builtin_assume)
#define kassume(cond) __builtin_assume(cond)
#else
/* Reasonable portable fallback: keep UB confined to truly-false paths. */
#define kassume(cond)                            \
	do {                                     \
		if (unlikely(!(cond))) {         \
			__builtin_unreachable(); \
		}                                \
	} while (0)
#endif
#endif
