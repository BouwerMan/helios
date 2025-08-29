/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _ARCH_SYSCALL_H
#define _ARCH_SYSCALL_H
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <asm/syscall.h>

extern long __syscall0(long n);
extern long __syscall1(long n, long a1);
extern long __syscall2(long n, long a1, long a2);
extern long __syscall3(long n, long a1, long a2, long a3);

extern long
__syscall6(long n, long a1, long a2, long a3, long a4, long a5, long a6);

#ifdef __cplusplus
}
#endif

#endif /* _ARCH_SYSCALL_H */
