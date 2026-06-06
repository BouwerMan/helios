/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

struct ktest {
	const char* name;
	int (*fn)(void); // 0 = pass, nonzero = fail
};

#define KTEST(test_name)                                       \
	static int test_name(void);                            \
	[[gnu::used, gnu::section(".ktests")]]                 \
	static const struct ktest __ktest_desc_##test_name = { \
		.name = #test_name,                            \
		.fn = (test_name)                              \
	};                                                     \
	static int test_name(void)

[[noreturn]]
void ktest_run_all();
