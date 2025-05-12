// Holds lots of useful functions and such
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

void panic(char* message);

#endif /* SYS_H */
