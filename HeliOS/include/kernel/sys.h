#ifndef _SYS_H
#define _SYS_H

// TODO: bring in asm.h

/* Types */
// #define NULL ((void*)0UL)
// typedef unsigned long uintptr_t;
// typedef long size_t;
// typedef unsigned int uint32_t;

/// Various defines
/* Kernel Strings */
#define KERNEL_NAME "HELIOS"
#define KERNEL_VERSION "0.0.0"

#define MEM_START 0x00100000
#define KERNEL_OFFSET 0xC0000000

/// Macros
#define CEIL_DIV(a, b) (((a + b) - 1) / b)

void panic(char* message);

#endif /* SYS_H */
