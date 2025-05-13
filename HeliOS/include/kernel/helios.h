// Holds helper macros and typedefs
#pragma once

#include "../../arch/x86_64/gdt.h"
#include "../../arch/x86_64/interrupts/idt.h"

#include <kernel/screen.h>
#include <kernel/tasks/scheduler.h>
#include <stdint.h>

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

#define __PACKED __attribute__((packed))

enum ERROR_CODES {
	ENONE = 0,
	EOOM,
};

struct kernel_context {
	struct gdt_ptr* gdt;
	struct idtr* idtr;
	struct scheduler_queue* squeue;
	struct screen_info* screen;
};

extern struct kernel_context kernel;
