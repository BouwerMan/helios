/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <kernel/helios.h>

#define PAGE_SIZE 0x1000ULL
_Static_assert(IS_POWER_OF_TWO(PAGE_SIZE) == true, "PAGE_SIZE must be power of 2");

#define ZONE_DMA_BASE  0x0
#define ZONE_DMA_LIMIT 0xffffffULL

#define ZONE_DMA32_BASE	 0x1000000ULL
#define ZONE_DMA32_LIMIT 0xffffffffULL

#define ZONE_NORMAL_BASE  0x100000000ULL
#define ZONE_NORMAL_LIMIT UINT64_MAX

#define KERNEL_VIRT_BASE 0xFFFFFFFF80000000ULL
#define HHDM_OFFSET	 0xffff800000000000ULL

// Physical memory
#define ZONE_NORMAL (1 << 0) // Over 4 GiB
#define ZONE_DMA32  (1 << 1) // Under 4 GiB
#define ZONE_DMA    (1 << 2) // Under 16 MiB (unimplemented lmao)

// Virtual memory
#define ALLOC_KERNEL (1 << 3)
#define ALLOC_USER   (1 << 4)
#define ALLOC_KDMA32 (ALLOC_KERNEL | ZONE_DMA32)
