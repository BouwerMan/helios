/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdint.h>

#include <kernel/bootinfo.h>
#include <kernel/compiler_attributes.h>
#include <kernel/types.h>
#include <limine.h>
#include <util/list.h>

/* Kernel Strings */
#define KERNEL_NAME    "HELIOS"
#define KERNEL_VERSION "0.0.0"

/// Macros

/**
 * @brief Computes the ceiling of the division of two numbers.
 * 
 * @param a The numerator.
 * @param b The denominator.
 * @return The smallest integer greater than or equal to a / b.
 */
#define CEIL_DIV(a, b)                  \
	__extension__({                 \
		__typeof__(a) _a = (a); \
		__typeof__(b) _b = (b); \
		((_a + _b - 1) / _b);   \
	})

/**
 * @brief Checks if a number is a power of two.
 * 
 * @param n The number to check.
 * @return True if n is a power of two, false otherwise.
 */
#define IS_POWER_OF_TWO(n) (n && !(n & (n - 1)))

/**
 * @brief Aligns a given size up to the nearest multiple of the specified alignment.
 * 
 * @param size The size to align.
 * @param align The alignment boundary.
 * @return The aligned size.
 */
#define ALIGN_UP(size, align) (((size + align - 1) / align) * align)

#define MAX(a, b)                       \
	__extension__({                 \
		__typeof__(a) _a = (a); \
		__typeof__(b) _b = (b); \
		_a > _b ? _a : _b;      \
	})

#define MIN(a, b)                       \
	__extension__({                 \
		__typeof__(a) _a = (a); \
		__typeof__(b) _b = (b); \
		_a < _b ? _a : _b;      \
	})

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/**
 * @brief Macro to indicate that the given expression is unlikely to be true.
 * @param expr The expression to evaluate.
 */
#define unlikely(expr) __builtin_expect(!!(expr), 0)

/**
 * @brief Macro to indicate that the given expression is likely to be true.
 * @param expr The expression to evaluate.
 */
#define likely(expr) __builtin_expect(!!(expr), 1)

#define BOCHS_BREAKPOINT (asm volatile("xchgw %bx, %bx"))
#define QEMU_BREAKPOINT	 (__asm__ volatile("jmp $"))

#define QEMU_SHUTDOWN outw(0x604, 0x2000)

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

#define KERNEL_STACK_SIZE_PAGES 16 // 16 pages of stack, 64 KiB

static inline void halt()
{
	__asm__ volatile("hlt");
}

enum ERROR_CODES {
	ENONE = 0,
	EOOM,
	EALIGN,
	ENULLPTR,

};

struct kernel_context {
	uint64_t* pml4;
	struct gdt_ptr* gdt;
	struct idtr* idtr;
	struct scheduler_queue* squeue;
	struct screen_info* screen;

	struct bootinfo bootinfo;

	struct list slab_caches;
};

extern struct kernel_context kernel;

// Both of these are located in kernel.c

void init_kernel_structure();
void kernel_main();
