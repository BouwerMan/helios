/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdint.h>

#include <kernel/compiler_attributes.h>

struct tss_descriptor {
	uint16_t limit_low;	// bits 0–15
	uint16_t base_low;	// bits 16–31
	uint8_t base_middle1;	// bits 32–39
	uint8_t type : 4;	// bits 40–43
	uint8_t zero1 : 1;	// bit 44: S (should be 0)
	uint8_t dpl : 2;	// bits 45–46
	uint8_t present : 1;	// bit 47
	uint8_t limit_high : 4; // bits 48–51
	uint8_t avl : 1;	// bit 52
	uint8_t zero2 : 2;	// bits 53–54: must be zero
	uint8_t gran : 1;	// bit 55: must be 0 for TSS
	uint8_t base_middle2;	// bits 56–63

	uint32_t base_high; // bits 64–95
	uint32_t reserved;  // bits 96–127: must be zero
} __packed;

/**
 * @brief 64-bit TSS
 */
struct tss_entry {
	uint32_t reserved_0;
	uint64_t rsp[3];
	uint64_t reserved_1;
	uint64_t ist[7];
	uint64_t reserved_2;
	uint16_t reserved_3;
	uint16_t iomap_base;
} __packed;
