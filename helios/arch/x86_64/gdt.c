/**
 * @file arch/x86_64/gdt.c
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

#include "gdt.h"

/* Our GDT, with 3 entries, and finally our special GDT pointer */
struct gdt_entry gdt[3];
struct gdt_ptr gp;

/* This will be a function in gdt.asm. We use this to properly
 *  reload the new segment registers */
extern void gdt_flush(struct gdt_ptr* gp);

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
void gdt_set_gate(uint8_t index, uint64_t base, uint32_t limit, uint8_t access, uint8_t gran)
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
	/* Setup the GDT pointer and limit */
	gp.limit = (sizeof(struct gdt_entry) * 3) - 1;
	gp.offset = gdt;

	/* Our NULL descriptor */
	gdt_set_gate(0, 0, 0, 0, 0);		 // NULL segment, offset 0x0000
	gdt_set_gate(1, 0, 0xFFFFF, 0x9A, 0xA0); // Kernel code segment, offset 0x0008
	gdt_set_gate(2, 0, 0xFFFFF, 0x92, 0xA0); // Kernel data segment, offset 0x0010

	/* Flush out the old GDT and install the new changes! */
	gdt_flush(&gp);
}
