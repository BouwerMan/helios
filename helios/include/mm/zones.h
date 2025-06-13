/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#define ZONE_DMA_BASE  0x0
#define ZONE_DMA_LIMIT 0xffffffULL

#define ZONE_DMA32_BASE	 0x1000000ULL
#define ZONE_DMA32_LIMIT 0xffffffffULL

#define ZONE_NORMAL_BASE  0x100000000ULL
#define ZONE_NORMAL_LIMIT UINTPTR_MAX

enum MEM_ZONE {
	MEM_ZONE_DMA,	 // For devices that require DMA
	MEM_ZONE_DMA32,	 // For devices that require 32-bit DMA
	MEM_ZONE_NORMAL, // Normal memory zone

	MEM_NUM_ZONES, // How many zones we have

	MEM_ZONE_INVALID
};
