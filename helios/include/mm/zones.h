/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdint.h>

#include <kernel/types.h>

static constexpr uptr ZONE_DMA_BASE  = 0x0;
static constexpr uptr ZONE_DMA_LIMIT = 0xffffff;

static constexpr uptr ZONE_DMA32_BASE  = 0x1000000;
static constexpr uptr ZONE_DMA32_LIMIT = 0xffffffff;

static constexpr uptr ZONE_NORMAL_BASE	= 0x100000000;
static constexpr uptr ZONE_NORMAL_LIMIT = UINTPTR_MAX;

enum MEM_ZONE {
	MEM_ZONE_DMA,	 // For devices that require DMA
	MEM_ZONE_DMA32,	 // For devices that require 32-bit DMA
	MEM_ZONE_NORMAL, // Normal memory zone

	MEM_NUM_ZONES, // How many zones we have

	MEM_ZONE_INVALID
};
