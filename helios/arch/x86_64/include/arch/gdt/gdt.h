/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdint.h>

#define GDT_ENTRIES 7 /**< The number of entries in the Global Descriptor Table (GDT). */

/**
 * The offset in the GDT where the TSS is located. This is the 5th entry in the GDT, so 4 * 8 = 32 bytes,
 * plus the first 8 bytes for the GDT pointer.
 */
#define TSS_OFFSET 0x28

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

void gdt_init();
void gdt_flush();
void set_tss_rsp(uint64_t rsp0);
