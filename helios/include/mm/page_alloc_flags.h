/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#define ZONE_BITS 2			   // Number of bits to represent the memory zone in flags
#define ZONE_MASK ((1UL << ZONE_BITS) - 1) // Mask for the zone bits

typedef enum ALLOC_FLAGS {
	AF_NORMAL = 0,	     // Normal zone allocation
	AF_DMA = (1 << 0),   // DMA zone allocation
	AF_DMA32 = (1 << 1), // DMA32 zone allocation

	AF_KERNEL = AF_NORMAL
} aflags_t;
