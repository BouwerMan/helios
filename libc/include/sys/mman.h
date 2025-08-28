/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _SYS_MMAN_H
#define _SYS_MMAN_H
#pragma once

#include <helios/mmap.h>
#include <stddef.h>
#include <sys/types.h>

void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset);

#endif /* _SYS_MMAN_H */
