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
#define CEIL_DIV(a, b) (((a + b) - 1) / b)

#define BOCHS_BREAKPOINT (asm volatile("xchgw %bx, %bx"))

struct kernel_context {
	struct gdt_ptr* gdt;
	struct idtr* idtr;
	struct scheduler_queue* squeue;
	struct screen_info* screen;
};

extern struct kernel_context kernel;
