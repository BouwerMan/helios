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
#define KERNEL_NAME    "HELIOS"
#define KERNEL_VERSION "0.0.0"

#define MEM_START     0x00100000
#define KERNEL_OFFSET 0xC0000000

#ifdef __KDEBUG__
#define DEBUG 1
#else
#define DEBUG 0
#endif

/// Macros
#define CEIL_DIV(a, b) (((a + b) - 1) / b)

#define BOCHS_BREAKPOINT (asm volatile("xchgw %bx, %bx"))

#define dprintf(fmt, ...)                                                                                              \
    do {                                                                                                               \
        if (DEBUG) printf("%s:%d:%s(): " fmt, __FILE__, __LINE__, __func__, __VA_ARGS__);                              \
    } while (0)

#define dputs(msg)                                                                                                     \
    do {                                                                                                               \
        if (DEBUG) printf("%s:%d:%s(): " msg, __FILE__, __LINE__, __func__);                                           \
    } while (0)

void panic(char* message);

#endif /* SYS_H */
