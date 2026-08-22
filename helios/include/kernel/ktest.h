/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "kernel/compiler_attributes.h"
#include "kernel/types.h"
#include "lib/log.h"

struct ktest {
	const char* name;
	int (*fn)(void);
	u32 flags;
};

#define KTEST_NO_PREEMPT (1u << 0) /* run with interrupts disabled */

#define KTEST_FLAGS(test_name, test_flags)                                              \
	static int test_name(void);                                                     \
	[[gnu::used, gnu::section(".ktests"), gnu::aligned(8)]]                         \
	static const struct ktest __ktest_desc_##test_name = { .name = #test_name,      \
							       .fn = (test_name),       \
							       .flags = (test_flags) }; \
	static int test_name(void)

#define KTEST(test_name) KTEST_FLAGS(test_name, 0)

[[noreturn]]
void ktest_run_all();

#define KTEST_CHECK(cond, expr_str, action)                                                  \
	do {                                                                                 \
		if (!(cond)) {                                                               \
			log_error("%s:%d: ASSERT(%s) failed", __FILE__, __LINE__, expr_str); \
			action;                                                              \
		}                                                                            \
	} while (0)

/* No cleanup needed: bail straight out of the test with rc = 1 */
#define KTEST_ASSERT_EQ(a, b) KTEST_CHECK((a) == (b), #a " == " #b, return 1)
#define KTEST_ASSERT_NE(a, b) KTEST_CHECK((a) != (b), #a " != " #b, return 1)
#define KTEST_ASSERT_TRUE(x)  KTEST_CHECK((x), #x, return 1)
#define KTEST_ASSERT_FALSE(x) KTEST_CHECK(!(x), "!(" #x ")", return 1)

/* Cleanup needed: jump to a labeled cleanup block instead of returning */
#define KTEST_ASSERT_EQ_GOTO(a, b, label) KTEST_CHECK((a) == (b), #a " == " #b, goto label)
#define KTEST_ASSERT_NE_GOTO(a, b, label) KTEST_CHECK((a) != (b), #a " != " #b, goto label)
#define KTEST_ASSERT_TRUE_GOTO(x, label)  KTEST_CHECK((x), #x, goto label)
#define KTEST_ASSERT_FALSE_GOTO(x, label) KTEST_CHECK(!(x), "!(" #x ")", goto label)
