/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "kernel/types.h"

#define __HAVE_ARCH_MEMSET 1
void* __arch_memset(void* restrict s, int c, size_t n);
u16* __arch_memset16(u16* restrict s, u16 v, size_t n);
u32* __arch_memset32(u32* restrict s, u32 v, size_t n);
u64* __arch_memset64(u64* restrict s, u64 v, size_t n);
