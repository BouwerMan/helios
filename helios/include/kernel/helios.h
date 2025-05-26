/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdint.h>

#include <util/list.h>

typedef uint8_t u8;
typedef int8_t i8;
typedef uint16_t u16;
typedef int16_t i16;
typedef uint32_t u32;
typedef int32_t i32;
typedef uint64_t u64;
typedef int64_t i64;

/// Macros

/**
 * @brief Computes the ceiling of the division of two numbers.
 * 
 * @param a The numerator.
 * @param b The denominator.
 * @return The smallest integer greater than or equal to a / b.
 */
#define CEIL_DIV(a, b) (((a + b) - 1) / b)

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

#define BOCHS_BREAKPOINT (asm volatile("xchgw %bx, %bx"))
#define QEMU_BREAKPOINT	 (__asm__ volatile("jmp $"))

#define __PACKED __attribute__((packed))

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

	struct list slab_caches;
};

extern struct kernel_context kernel;
