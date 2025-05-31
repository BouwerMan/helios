/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdint.h>

#include <kernel/types.h>
#include <limine.h>
#include <util/list.h>

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

// Bunch of attribute defines

#define __packed __attribute__((packed))

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

#if __has_attribute(__fallthrough__)
#define fallthrough __attribute__((__fallthrough__))
#else
#define fallthrough \
	do {        \
	} while (0) /* fallthrough */
#endif

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
	struct limine_memmap_response* memmap; // Memmap from limine
	struct scheduler_queue* squeue;
	struct screen_info* screen;

	struct list slab_caches;
};

extern struct kernel_context kernel;
