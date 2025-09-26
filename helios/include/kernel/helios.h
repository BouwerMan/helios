/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdint.h>

#include "kernel/align.h"
#include "kernel/bitops.h"
#include <kernel/bootinfo.h>
#include <kernel/compiler_attributes.h>
#include <kernel/types.h>
#include <lib/list.h>
#include <limine.h>

/* Kernel Strings */
#define KERNEL_NAME    "HELIOS"
#define KERNEL_VERSION "0.0.0"

/// Macros

#define UNIMPLEMENTED __builtin_unreachable()

/**
 * @brief Computes the ceiling of the division of two numbers.
 * 
 * @param a The numerator.
 * @param b The denominator.
 * @return The smallest integer greater than or equal to a / b.
 */
#define CEIL_DIV(a, b)                  \
	({                              \
		__typeof__(a) _a = (a); \
		__typeof__(b) _b = (b); \
		((_a + _b - 1) / _b);   \
	})

/**
 * @brief Return the maximum of two values.
 */
#define MAX(a, b)                                  \
	({                                         \
		__typeof__(a) _max_a = (a);        \
		__typeof__(b) _max_b = (b);        \
		_max_a > _max_b ? _max_a : _max_b; \
	})

/**
 * @brief Return the minimum of two values.
 */
#define MIN(a, b)                                  \
	({                                         \
		__typeof__(a) _min_a = (a);        \
		__typeof__(b) _min_b = (b);        \
		_min_a < _min_b ? _min_a : _min_b; \
	})

/**
 * @brief Clamp a value between lower and upper bounds.
 */
#define CLAMP(val, lo, hi)                             \
	({                                             \
		__typeof__(val) _clamp_val = (val);    \
		__typeof__(lo) _clamp_lo = (lo);       \
		__typeof__(hi) _clamp_hi = (hi);       \
		(_clamp_val < _clamp_lo) ? _clamp_lo : \
		(_clamp_val > _clamp_hi) ? _clamp_hi : \
					   _clamp_val; \
	})

/**
 * @brief Get the number of elements in a statically sized array.
 * @param arr Array variable (not a pointer).
 * @return Number of elements.
 */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

// -------- branch prediction helpers --------
/**
 * @brief Macro to indicate that the given expression is unlikely to be true.
 * @param expr The expression to evaluate.
 */
#ifndef unlikely
#if __has_builtin(__builtin_expect) || defined(__GNUC__)
#define unlikely(expr) __builtin_expect(!!(expr), 0)
#else
#define unlikely(expr) (!!(expr))
#endif
#endif

/**
 * @brief Macro to indicate that the given expression is likely to be true.
 * @param expr The expression to evaluate.
 */
#ifndef likely
#if __has_builtin(__builtin_expect) || defined(__GNUC__)
#define likely(expr) __builtin_expect(!!(expr), 1)
#else
#define likely(expr) (!!(expr))
#endif
#endif

#define BOCHS_BREAKPOINT (asm volatile("xchgw %bx, %bx"))
#define QEMU_BREAKPOINT	 (__asm__ volatile("jmp $"))

#define QEMU_SHUTDOWN()                 \
	({                              \
		outword(0x604, 0x2000); \
		outb(0xF4, 0);          \
	})

#define TESTING_HEADER                                                                        \
	"\n\n*****************************************************************************\n" \
	"BEGIN TEST: %s\n"                                                                    \
	"Note: Error messages during this test are expected.\n"                               \
	"      Assertions indicate failure.\n"                                                \
	"*****************************************************************************\n"

#define TESTING_FOOTER                                                                        \
	"\n\n*****************************************************************************\n" \
	"END TEST: %s\n"                                                                      \
	"Test completed successfully. All expected conditions were met.\n"                    \
	"*****************************************************************************\n"

static inline void halt()
{
	__asm__ volatile("hlt");
}

static inline unsigned long long get_rsp_value()
{
	unsigned long long rsp_val;
	__asm__ __volatile__("movq %%rsp, %0" : "=r"(rsp_val));
	return rsp_val;
}

struct kernel_context {
	uint64_t* pml4;
	struct gdt_ptr* gdt;
	struct idtr* idtr;
	struct scheduler_queue* squeue;
	struct screen_info* screen;

	struct klog_ring* klog;

	struct bootinfo bootinfo;

	struct list_head slab_caches;
};

extern struct kernel_context kernel;

// Both of these are located in kernel.c

void init_kernel_structure();
void kernel_main();
