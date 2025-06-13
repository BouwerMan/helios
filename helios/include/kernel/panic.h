/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <util/log.h>

[[noreturn]]
void panic(const char* message);

[[noreturn, maybe_unused]]
static void __kassert_fail(const char* expr, const char* file, int line, const char* func)
{
	set_log_mode(LOG_DIRECT);
	log_error("Assertion failed: %s, file: %s, line: %d, function: %s", expr, file, line, func);
	panic("Kernel assertion failed, halting system.");
}

#ifdef __KDEBUG__
#define kassert(expr) ((expr) ? (void)0 : __kassert_fail(#expr, __FILE__, __LINE__, __func__))
#else
#define kassert(expr) ((void)0)
#endif
