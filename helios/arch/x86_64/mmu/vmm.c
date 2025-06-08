/**
 * @file arch/x86_64/mmu/vmm.c
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

#include <stdint.h>

#include <arch/x86_64/memcpy.h>
#include <arch/x86_64/mmu/vmm.h>
#include <kernel/helios.h>
#include <kernel/panic.h>
#include <mm/page_alloc.h>
#include <util/log.h>

uint64_t* vmm_create_address_space()
{
	// pml4 has 512 entries, each 8 bytes. which means it is 4096 (1 page) bytes in size.
	uint64_t* pml4 = (uint64_t*)get_free_pages(AF_KERNEL, PML4_SIZE_PAGES);
	if (!pml4) {
		log_error("Failed to allocate PML4");
		panic("Out of memory");
	}

	__fast_memcpy(pml4, kernel.pml4, PAGE_SIZE);

	return pml4;
}

void vmm_init()
{
	// Init new address space, then copy from limine

	kernel.pml4 = (uint64_t*)PHYS_TO_HHDM(vmm_read_cr3());
	log_debug("Current PML4: %p", (void*)kernel.pml4);

	kernel.pml4 = vmm_create_address_space();
	// Switch to this cr3
}
