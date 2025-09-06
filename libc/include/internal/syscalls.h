/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _INTERNAL_SYSCALLS_H
#define _INTERNAL_SYSCALLS_H
#pragma once

#include <stddef.h>
#include <sys/types.h>

ssize_t __libc_read(int fd, void* buf, size_t count);
ssize_t __libc_write(int fd, const void* buf, size_t count);

/**
 * __syscall_* wrappers:
 */

ssize_t __syscall_read(int fd, void* buf, size_t count);
ssize_t __syscall_write(int fd, const void* buf, size_t count);

void* __syscall_get_errno_ptr();

#endif /* _INTERNAL_SYSCALLS_H */
