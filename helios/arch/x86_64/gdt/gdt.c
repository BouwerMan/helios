/**
 * @file arch/x86_64/gdt/gdt.c
 *
 * Copyright (C) 2025  Dylan Parks
 *
 * This file is part of HeliOS
 *
 * HeliOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <string.h>

#include <arch/gdt/gdt.h>

#include "tss.h"

/* Our GDT, with 6 entries, and finally our special GDT pointer */
struct gdt_entry gdt[GDT_ENTRIES];
struct gdt_ptr gp;
struct tss_entry tss;

/* This will be a function in gdt.asm. We use this to properly
 *  reload the new segment registers */
extern void __gdt_flush(struct gdt_ptr* gp);
extern void __tss_flush(uint64_t tss_address);

void gdt_flush()
{
	/* Call the assembly function to flush the GDT */
	__gdt_flush(&gp);
	__tss_flush(TSS_OFFSET);
}

/**
 * @brief Sets up a descriptor in the Global Descriptor Table (GDT).
 *
 * This function configures a GDT entry with the specified base address, limit,
 * access flags, and granularity. The GDT is used by the CPU to define memory
 * segments and their properties.
 *
 * @param index The index of the GDT entry to configure.
 * @param base The base address of the memory segment.
 * @param limit The limit of the memory segment (size - 1).
 * @param access The access flags that define the segment's type and permissions.
 * @param gran The granularity and size flags for the segment.
 */
static void gdt_set_gate(uint8_t index, uint64_t base, uint32_t limit, uint8_t access, uint8_t gran)
{
	/* Setup the descriptor base address */
	gdt[index].base_low = (base & 0xFFFF);
	gdt[index].base_middle = (base >> 16) & 0xFF;
	gdt[index].base_high = (base >> 24) & 0xFF;

	/* Setup the descriptor limits */
	gdt[index].limit_low = (limit & 0xFFFF);
	gdt[index].granularity = ((limit >> 16) & 0x0F);

	/* Finally, set up the granularity and access flags */
	gdt[index].granularity |= (gran & 0xF0);
	gdt[index].access = access;
}

static void gdt_set_tss_descriptor(void* tss_ptr, size_t tss_size)
{
	uintptr_t base = (uintptr_t)tss_ptr;
	uint32_t limit = (uint32_t)(tss_size - 1);

	// GDT entry 5 (first 8 bytes of the descriptor)
	gdt[5].limit_low = limit & 0xFFFF;
	gdt[5].base_low = base & 0xFFFF;
	gdt[5].base_middle = (base >> 16) & 0xFF;
	gdt[5].access = 0x89; // 10001001b: present, system, type = 9 (TSS 64-bit available)
	gdt[5].granularity = ((limit >> 16) & 0x0F);
	gdt[5].granularity |= (0 << 4); // AVL = 0, L = 0, D/B = 0, G = 0
	gdt[5].base_high = (base >> 24) & 0xFF;

	// GDT entry 6 (high 64 bits of the descriptor)
	struct {
		uint32_t base_upper;
		uint32_t reserved;
	} __packed* tss_high = (void*)&gdt[6];

	tss_high->base_upper = (uint32_t)((base >> 32) & 0xFFFFFFFF);
	tss_high->reserved = 0;
}

/**
 * @brief Initializes the Global Descriptor Table (GDT).
 *
 * This function sets up the GDT pointer and defines the first three entries
 * in the GDT: a NULL descriptor, a kernel code segment, and a kernel data segment.
 * After setting up the entries, it flushes the old GDT and loads the new one
 * using the `gdt_flush` function.
 *
 * @note This function should be called during system initialization to ensure
 *       proper memory segmentation for the kernel.
 */
void gdt_init()
{
	memset((unsigned char*)&gdt, 0, sizeof(struct gdt_entry) * 6); // Clear the GDT

	/* Setup the GDT pointer and limit */
	gp.limit = (sizeof(struct gdt_entry) * GDT_ENTRIES) - 1;
	gp.offset = gdt;

	/* Our NULL descriptor */
	gdt_set_gate(0, 0, 0, 0, 0);		   // NULL segment, offset 0x0000
	gdt_set_gate(1, 0, 0xFFFFF, 0x9A, 0xA0);   // Kernel code segment, offset 0x0008
	gdt_set_gate(2, 0, 0xFFFFF, 0x92, 0xA0);   // Kernel data segment, offset 0x0010
	gdt_set_gate(3, 0, 0xFFFFF, 0xFA, 0xA0);   // User code segment, offset 0x0018
	gdt_set_gate(4, 0, 0xFFFFF, 0xF2, 0xA0);   // User data segment, offset 0x0020
	gdt_set_tss_descriptor(&tss, sizeof(tss)); // TSS segment, offset 0x0028

	/* Flush out the old GDT and install the new changes! */
	gdt_flush();
}

void set_tss_rsp(uint64_t rsp0)
{
	tss.rsp[0] = rsp0;
}
