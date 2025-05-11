// Holds helper macros and typedefs
#pragma once

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
