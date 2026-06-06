/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "kernel/compiler_attributes.h"
#include "kernel/types.h"

struct ktest {
	const char* name;
	int (*fn)(void);
	u32 flags;
};

#define KTEST_NO_PREEMPT (1u << 0) /* run with interrupts disabled */

#define KTEST_FLAGS(test_name, test_flags)                      \
	static int test_name(void);                             \
	[[gnu::used, gnu::section(".ktests"), gnu::aligned(8)]] \
	static const struct ktest __ktest_desc_##test_name = {  \
		.name = #test_name,                             \
		.fn = (test_name),                              \
		.flags = (test_flags)                           \
	};                                                      \
	static int test_name(void)

#define KTEST(test_name) KTEST_FLAGS(test_name, 0)

[[noreturn]]
void ktest_run_all();
