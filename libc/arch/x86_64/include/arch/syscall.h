/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _ARCH_SYSCALL_H
#define _ARCH_SYSCALL_H
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// TODO: Move to a central location like in the kernel headers
// Then we just include it here
enum HELIOS_SYSCALLS {
	SYS_READ,
	SYS_WRITE,
};

extern long __syscall0(long n);
extern long __syscall1(long n, long a1);
extern long __syscall2(long n, long a1, long a2);
extern long __syscall3(long n, long a1, long a2, long a3);

#ifdef __cplusplus
}
#endif

#endif /* _ARCH_SYSCALL_H */
