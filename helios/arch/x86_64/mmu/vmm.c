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

/**
* Unlike my original vmm implementation, this one only focuses on paging and address space magaement.
* Overview:
* 	1. Kernel inits bootmem
* 	2. Kernel inits page_alloc
* 	3. Kernel decommissions bootmem which then releases limine reclaimable resources
* 	4. We init our kernel address space
*
* We will have a mapping of the entire physical memory space at hhdm_offset.
*/

#include <stdint.h>

#include <arch/x86_64/memcpy.h>
#include <arch/x86_64/mmu/vmm.h>
#include <kernel/bootinfo.h>
#include <kernel/helios.h>
#include <kernel/panic.h>
#include <mm/page.h>
#include <mm/page_alloc.h>
#include <util/log.h>

static bool _is_valid_type(size_t type)
{
	return (type == LIMINE_MEMMAP_USABLE) || (type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE) ||
	       (type == LIMINE_MEMMAP_EXECUTABLE_AND_MODULES) || (type == LIMINE_MEMMAP_FRAMEBUFFER);
}

static void _map_memmap_entry(uint64_t* pml4, struct bootinfo_memmap_entry* entry, uintptr_t exe_virt_base)
{
	flags_t flags = PAGE_PRESENT | PAGE_WRITE;
	switch (entry->type) {
	case LIMINE_MEMMAP_USABLE:
	case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
		flags |= CACHE_WRITE_BACK | PAGE_NO_EXECUTE;
		break;
	case LIMINE_MEMMAP_FRAMEBUFFER:
		flags |= CACHE_WRITE_COMBINING | PAGE_NO_EXECUTE;
		break;
	case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES:
		flags |= CACHE_WRITE_BACK;
		break;
	default:
		return;
	}

	uintptr_t start = entry->base;
	uintptr_t end = entry->base + entry->length;
	log_debug("Mapping [%lx-%lx), type: %lu", start, end, entry->type);
	for (size_t phys = start; phys < end; phys += PAGE_SIZE) {
		map_page(pml4, PHYS_TO_HHDM(phys), phys, flags);

		// Manually map executable areas
		if (entry->type != LIMINE_MEMMAP_EXECUTABLE_AND_MODULES) continue;
		uintptr_t exe_virt = (phys - start) + exe_virt_base;
		map_page(pml4, exe_virt, phys, flags & ~PAGE_NO_EXECUTE);
	}
}

void vmm_init()
{
	// Init new address space, then copy from limine
	struct bootinfo* bootinfo = &kernel.bootinfo;
	if (!bootinfo->valid) panic("bootinfo marked not valid");

	kernel.pml4 = (uint64_t*)get_free_pages(AF_KERNEL, PML4_SIZE_PAGES);
	log_debug("Current PML4: %p", (void*)kernel.pml4);
	for (size_t i = 0; i < bootinfo->memmap_entry_count; i++) {
		struct bootinfo_memmap_entry* entry = &bootinfo->memmap[i];
		_map_memmap_entry(kernel.pml4, entry, bootinfo->executable.virtual_base);
	}

	vmm_load_cr3(HHDM_TO_PHYS(kernel.pml4));

	// TODO: Reclaim from the bootloader, not really sure how to do this though since our stack is in that region
}

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

#define VADDR_PML4_INDEX(vaddr) (((uintptr_t)(vaddr) >> 39) & 0x1FF)
#define VADDR_PDPT_INDEX(vaddr) (((uintptr_t)(vaddr) >> 30) & 0x1FF)
#define VADDR_PD_INDEX(vaddr)	(((uintptr_t)(vaddr) >> 21) & 0x1FF)
#define VADDR_PT_INDEX(vaddr)	(((uintptr_t)(vaddr) >> 12) & 0x1FF)

/**
 * @brief Walks the page table hierarchy to locate or create a page table entry.
 * @param pml4 Pointer to the PML4 table.
 * @param vaddr The virtual address to resolve.
 * @param create Whether to create missing entries in the hierarchy.
 * @param flags Flags to set for newly created entries.
 *
 * This function traverses the page table hierarchy (PML4 -> PDPT -> PD -> PT)
 * to locate the page table entry corresponding to the given virtual address.
 * If @create is true, missing entries in the hierarchy are created with the
 * specified @flags. If @create is false and a required entry is missing, the
 * function returns NULL.
 *
 * @return A pointer to the page table entry corresponding to the given virtual address,
 *         or NULL if the entry does not exist and @create is false.
 */
uint64_t* walk_page_table(uint64_t* pml4, uintptr_t vaddr, bool create, flags_t flags)
{
	// Ensure the virtual address is canonical
	if ((vaddr >> 48) != 0 && (vaddr >> 48) != 0xFFFF) return NULL;

	// Mask the flags to ensure only valid bits are used
	flags &= FLAGS_MASK;

	// Get the PML4 index for the virtual address
	uint64_t pml4_i = VADDR_PML4_INDEX(vaddr);
	if ((pml4[pml4_i] & PAGE_PRESENT) == 0) {
		if (!create) return NULL;
		pml4[pml4_i] = HHDM_TO_PHYS(get_free_page(AF_KERNEL)) | flags;
	}

	// Get the PDPT from the PML4 entry
	uint64_t* pdpt = (uint64_t*)PHYS_TO_HHDM(pml4[pml4_i] & ~FLAGS_MASK); // Mask off flags
	uint64_t pdpt_i = VADDR_PDPT_INDEX(vaddr);
	if ((pdpt[pdpt_i] & PAGE_PRESENT) == 0) {
		if (!create) return NULL;
		pdpt[pdpt_i] = HHDM_TO_PHYS(get_free_page(AF_KERNEL)) | flags;
	}

	// Get the PD from the PDPT entry
	uint64_t* pd = (uint64_t*)PHYS_TO_HHDM(pdpt[pdpt_i] & ~FLAGS_MASK); // Mask off flags
	uint64_t pd_i = VADDR_PD_INDEX(vaddr);
	if ((pd[pd_i] & PAGE_PRESENT) == 0) {
		if (!create) return NULL;
		pd[pd_i] = HHDM_TO_PHYS(get_free_page(AF_KERNEL)) | flags;
	}

	// Get the PT from the PD entry
	uint64_t* pt = (uint64_t*)PHYS_TO_HHDM(pd[pd_i] & ~FLAGS_MASK);
	uint64_t pt_i = VADDR_PT_INDEX(vaddr);

	// Return the pointer to the page table entry
	return pt + pt_i;
}

int map_page(uint64_t* pml4, uintptr_t vaddr, uintptr_t paddr, flags_t flags)
{
	if (!is_page_aligned(vaddr) || !is_page_aligned(paddr)) {
		log_error("Something isn't aligned right, vaddr: %lx, paddr: %lx", vaddr, paddr);
		return -1;
	}

	uint64_t* pte = walk_page_table(pml4, vaddr, true, flags & (PAGE_PRESENT | PAGE_WRITE));
	if (!pte || *pte & PAGE_PRESENT) {
		log_warn("Could not find pte or pte is already present");
		return -1;
	}

	*pte = paddr | flags;
	return 0;
}
