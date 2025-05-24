/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdint.h>

void gdt_set_gate(uint8_t index, uint64_t base, uint32_t limit, uint8_t access, uint8_t gran);
void gdt_init();

/* Defines a GDT entry. We say packed, because it prevents the
 *  compiler from doing things that it thinks is best: Prevent
 *  compiler "optimization" by packing */
struct gdt_entry {
	uint16_t limit_low;  /**< lower 16 bits of the segment limit. */
	uint16_t base_low;   /**< lower 16 bits of the base address. */
	uint8_t base_middle; /**< next 8 bits of the base address. */
	uint8_t access;	     /**< access flags defining segment type and permissions. */
	uint8_t granularity; /**< granularity, size flags, and upper 4 bits of the limit. */
	uint8_t base_high;   /**< final 8 bits of the base address. */
} __attribute__((packed));

/* Special pointer which includes the limit: The max bytes
 *  taken up by the GDT, minus 1. Again, this NEEDS to be packed */
struct gdt_ptr {
	uint16_t limit;		  /**< The size of the GDT in bytes minus 1. */
	struct gdt_entry* offset; /**< The memory address of the first GDT entry. */
} __attribute__((packed));
