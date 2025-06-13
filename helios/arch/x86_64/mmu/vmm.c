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
#include <string.h>

#include <arch/mmu/vmm.h>
#include <kernel/bootinfo.h>
#include <kernel/helios.h>
#include <kernel/panic.h>
#include <mm/page.h>
#include <mm/page_alloc.h>
#include <util/log.h>

static inline void* _alloc_page_table(aflags_t flags)
{
	return (void*)get_free_pages(flags, PML4_SIZE_PAGES);
}

static inline void _free_page_table(void* table)
{
	free_pages(table, PML4_SIZE_PAGES);
}

static void _map_memmap_entry(uint64_t* pml4, struct bootinfo_memmap_entry* entry, uintptr_t exe_virt_base)
{
	flags_t flags;
	switch (entry->type) {
	case LIMINE_MEMMAP_USABLE:
	case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
		flags = PAGE_PRESENT | PAGE_WRITE | CACHE_WRITE_BACK | PAGE_NO_EXECUTE;
		break;
	case LIMINE_MEMMAP_FRAMEBUFFER:
		flags = PAGE_PRESENT | PAGE_WRITE | CACHE_WRITE_COMBINING | PAGE_NO_EXECUTE;
		break;
	case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES:
		flags = PAGE_PRESENT | PAGE_WRITE | CACHE_WRITE_BACK;
		break;
	default:
		return;
	}

	uintptr_t start = entry->base;
	uintptr_t end	= entry->base + entry->length;
	log_debug("Mapping [%lx-%lx), type: %lu", start, end, entry->type);
	for (size_t phys = start; phys < end; phys += PAGE_SIZE) {
		map_page(pml4, PHYS_TO_HHDM(phys), phys, flags);

		// Manually map executable areas
		if (entry->type != LIMINE_MEMMAP_EXECUTABLE_AND_MODULES) continue;
		uintptr_t exe_virt = (phys - start) + exe_virt_base;
		map_page(pml4, exe_virt, phys, flags & ~PAGE_NO_EXECUTE);
	}
}

/**
 * @brief Initializes the virtual memory manager (VMM).
 *
 * This function sets up the kernel's address space by creating a new PML4
 * table and mapping memory regions based on the boot information provided
 * by the bootloader. It ensures that the kernel's memory layout is properly
 * initialized and ready for use.
 *
 * Steps:
 * 1. Validates the boot information structure.
 * 2. Allocates a new PML4 table for the kernel.
 * 3. Iterates through the memory map entries provided by the bootloader and
 *    maps them into the kernel's address space.
 * 4. Loads the new PML4 table into the CR3 register to activate the address space.
 */
void vmm_init()
{
	// Init new address space, then copy from limine
	struct bootinfo* bootinfo = &kernel.bootinfo;
	if (!bootinfo->valid) panic("bootinfo marked not valid");

	kernel.pml4 = _alloc_page_table(AF_KERNEL);
	log_debug("Current PML4: %p", (void*)kernel.pml4);
	for (size_t i = 0; i < bootinfo->memmap_entry_count; i++) {
		struct bootinfo_memmap_entry* entry = &bootinfo->memmap[i];
		_map_memmap_entry(kernel.pml4, entry, bootinfo->executable.virtual_base);
	}

	vmm_load_cr3(HHDM_TO_PHYS(kernel.pml4));
}

uint64_t* vmm_create_address_space()
{
	// pml4 has 512 entries, each 8 bytes. which means it is 4096 (1 page) bytes in size.
	uint64_t* pml4 = _alloc_page_table(AF_KERNEL);
	if (!pml4) {
		log_error("Failed to allocate PML4");
		panic("Out of memory");
	}

	memcpy(pml4, kernel.pml4, PAGE_SIZE);

	return pml4;
}

/**
 * @brief Invalidates a single page in the TLB (Translation Lookaside Buffer).
 *
 * @param vaddr The virtual address of the page to invalidate.
 */
static inline void invalidate(uintptr_t vaddr)
{
	__asm__ volatile("invlpg (%0)" ::"r"(vaddr) : "memory");
}

static inline size_t _page_table_index(uptr vaddr, int shift)
{
	return (vaddr >> shift) & 0x1FF;
}

#define _pml4_index(vaddr) _page_table_index(vaddr, 39)
#define _pdpt_index(vaddr) _page_table_index(vaddr, 30)
#define _pd_index(vaddr)   _page_table_index(vaddr, 21)
#define _pt_index(vaddr)   _page_table_index(vaddr, 12)

/**
 * @brief Walks the page table hierarchy to locate or create a page table entry.
 *
 * This function traverses the page table hierarchy (PML4 -> PDPT -> PD -> PT)
 * to locate the page table entry corresponding to the given virtual address.
 * If @create is true, missing entries in the hierarchy are created with the
 * specified @flags. If @create is false and a required entry is missing, the
 * function returns NULL.
 *
 * @param pml4   Pointer to the PML4 table.
 * @param vaddr  The virtual address to resolve.
 * @param create Whether to create missing entries in the hierarchy.
 * @param flags  Flags to set for newly created entries.
 *
 * @return       A pointer to the page table entry corresponding to the given
 *               virtual address, or NULL if the entry does not exist and
 *               @create is false.
 */
uint64_t* walk_page_table(uint64_t* pml4, uintptr_t vaddr, bool create, flags_t flags)
{
	// Ensure the virtual address is canonical
	if ((vaddr >> 48) != 0 && (vaddr >> 48) != 0xFFFF) return NULL;

	// Mask the flags to ensure only valid bits are used
	flags &= FLAGS_MASK;

	// Get the PML4 index for the virtual address
	uint64_t pml4_i = _pml4_index(vaddr);
	if ((pml4[pml4_i] & PAGE_PRESENT) == 0) {
		if (!create) return NULL;
		pml4[pml4_i] = HHDM_TO_PHYS(_alloc_page_table(AF_KERNEL)) | flags;
	}

	// Get the PDPT from the PML4 entry
	uint64_t* pdpt	= (uint64_t*)PHYS_TO_HHDM(pml4[pml4_i] & ~FLAGS_MASK); // Mask off flags
	uint64_t pdpt_i = _pdpt_index(vaddr);
	if ((pdpt[pdpt_i] & PAGE_PRESENT) == 0) {
		if (!create) return NULL;
		pdpt[pdpt_i] = HHDM_TO_PHYS(_alloc_page_table(AF_KERNEL)) | flags;
	}

	// Get the PD from the PDPT entry
	uint64_t* pd  = (uint64_t*)PHYS_TO_HHDM(pdpt[pdpt_i] & ~FLAGS_MASK); // Mask off flags
	uint64_t pd_i = _pd_index(vaddr);
	if ((pd[pd_i] & PAGE_PRESENT) == 0) {
		if (!create) return NULL;
		pd[pd_i] = HHDM_TO_PHYS(_alloc_page_table(AF_KERNEL)) | flags;
	}

	// Get the PT from the PD entry
	uint64_t* pt  = (uint64_t*)PHYS_TO_HHDM(pd[pd_i] & ~FLAGS_MASK);
	uint64_t pt_i = _pt_index(vaddr);

	// Return the pointer to the page table entry
	return pt + pt_i;
}

/**
 * @brief Maps a virtual address to a physical address in the page table.
 *
 * This function maps a virtual address to a physical address in the page table
 * with the specified flags. It ensures that both the virtual and physical
 * addresses are page-aligned. If the mapping already exists, or if there is
 * an alignment issue, the function returns an error.
 *
 * @param pml4   Pointer to the PML4 table.
 * @param vaddr  Virtual address to map.
 * @param paddr  Physical address to map to.
 * @param flags  Flags for the page table entry (e.g., PAGE_PRESENT, PAGE_WRITE).
 *
 * @return       0 on success, -1 on failure (e.g., misalignment or mapping issues).
 */
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

/**
 * @brief Retrieves the index for a specific level in the page table hierarchy.
 *
 * This function calculates the index of the entry in the page table for the
 * given level (PML4, PDPT, PD, or PT) based on the provided virtual address.
 *
 * @param level  The level in the page table hierarchy (0 = PML4, 3 = PT).
 * @param vaddr  The virtual address to calculate the index for.
 *
 * @return       The index for the specified level, or (size_t)-1 if the level is invalid.
 */
static size_t _get_index(int level, uintptr_t vaddr)
{
	switch (level) {
	case 0:
		return _pml4_index(vaddr);
	case 1:
		return _pdpt_index(vaddr);
	case 2:
		return _pd_index(vaddr);
	case 3:
		return _pt_index(vaddr);
	default:
		return (size_t)-1; // Invalid level
	}
}

/**
 * @brief Checks if a page table is empty.
 *
 * This function iterates through all entries in the given page table and
 * determines if all entries are zero (i.e., the table is empty).
 *
 * @param table  Pointer to the page table to check.
 *
 * @return       True if the table is empty, false otherwise.
 */
static bool _is_table_empty(uint64_t* table)
{
	// TODO: turn this into a memcmp (ideally architecture specific with rep cmpsb)
	for (size_t i = 0; i < PML4_ENTRIES; i++) {
		if (table[i] != 0) return false; // Found non-empty entry
	}
	return true;
}

/**
 * @brief Recursively prunes empty page tables in the hierarchy.
 *
 * This function traverses the page table hierarchy starting from the given
 * level and virtual address. It checks if the current table entry is present
 * and, if not, determines whether the table is empty. If the table is empty,
 * it clears the entry and frees the associated memory. For non-leaf levels,
 * the function recurses into child tables to prune them as well.
 *
 * @param table  Pointer to the current page table.
 * @param level  Current level in the page table hierarchy (0 = PML4, 3 = leaf).
 * @param vaddr  Virtual address to prune from.
 *
 * @return       True if the table is empty after pruning, false otherwise.
 */
static bool _prune_page_table_recursive(uint64_t* table, int level, uintptr_t vaddr)
{
	size_t index	= _get_index(level, vaddr);
	uintptr_t entry = table[index];

	// If the entry is not present, return early
	if ((entry & PAGE_PRESENT) == 0) {
		return _is_table_empty(table);
	}

	// If we are not at the leaf, we need to recurse
	if (level < 3) {
		uint64_t* child_table = (uint64_t*)PHYS_TO_HHDM(entry & PAGE_FRAME_MASK);
		if (_prune_page_table_recursive(child_table, level + 1, vaddr)) {
			table[index] = 0; // Clear the entry if child table was pruned
			_free_page_table(child_table);
			log_debug("Freed PT at level %d (vaddr: 0x%lx)", level, vaddr);
		}
	}

	return _is_table_empty(table);
}

/**
 * @brief Unmaps a virtual address from the page table.
 *
 * This function removes the mapping of a virtual address from the page table.
 * It ensures that the virtual address is page-aligned and checks if the page
 * is already unmapped. If the page is mapped, it clears the page table entry,
 * prunes empty page tables, and invalidates the TLB entry for the virtual address.
 *
 * @param pml4   Pointer to the PML4 table.
 * @param vaddr  Virtual address to unmap.
 *
 * @return       0 on success, -1 on failure (e.g., misalignment).
 *               Returns 0 if the page was already unmapped.
 */
int unmap_page(uint64_t* pml4, uintptr_t vaddr)
{
	if (!is_page_aligned(vaddr)) {
		log_error("Something isn't aligned right, vaddr: %lx", vaddr);
		return -1;
	}

	uint64_t* pte = walk_page_table(pml4, vaddr, false, 0);

	if (!pte || !(*pte & PAGE_PRESENT)) {
		return 0; // Already unmapped, nothing to do
	}

	*pte = 0;

	prune_page_tables(pml4, vaddr);
	invalidate(vaddr);

	return 0;
}

/**
 * @brief Prunes empty page tables recursively.
 *
 * This function traverses the page table hierarchy and removes empty tables
 * starting from the specified virtual address. It ensures that unused page
 * tables are freed to conserve memory.
 *
 * @param pml4   Pointer to the PML4 table.
 * @param vaddr  Virtual address to start pruning from.
 *
 * @return       0 on success.
 */
int prune_page_tables(uint64_t* pml4, uintptr_t vaddr)
{
	(void)_prune_page_table_recursive(pml4, 0, vaddr);

	return 0;
}

/**
 * Test function to validate the pruning of a single mapping in the page table.
 *
 * Steps:
 * 1. Allocates a fresh address space (PML4 table).
 * 2. Maps a test virtual address to a physical page.
 * 3. Unmaps the virtual address.
 * 4. Prunes the page tables to remove unused entries.
 * 5. Verifies that the PML4 entry is cleared after pruning.
 * 6. Cleans up allocated resources.
 *
 * This function logs the success or failure of each step and ensures that
 * the page table pruning logic works as expected.
 */
void vmm_test_prune_single_mapping(void)
{
	// 1. Allocate a fresh address space
	uint64_t* pml4 = _alloc_page_table(AF_KERNEL);

	// 2. Choose a test virtual address and physical page
	uintptr_t vaddr = 0x00007FFFFFFFE000; // Arbitrary, canonical, aligned
	uintptr_t paddr = (uintptr_t)HHDM_TO_PHYS(get_free_page(AF_KERNEL));

	log_info("Mapping page: virt=0x%lx -> phys=0x%lx", vaddr, paddr);
	int result = map_page(pml4, vaddr, paddr, PAGE_PRESENT | PAGE_WRITE | CACHE_WRITE_BACK);
	if (result != 0) {
		log_error("Failed to map test page");
		return;
	}

	// 3. Unmap the virtual address
	log_info("Unmapping page: 0x%lx", vaddr);
	result = unmap_page(pml4, vaddr);
	if (result != 0) {
		log_error("Failed to unmap test page");
		return;
	}

	// 4. Prune page tables
	log_info("Pruning page tables for vaddr 0x%lx", vaddr);
	prune_page_tables(pml4, vaddr);

	// 5. Verify that the PML4 entry is now 0
	size_t pml4_i = _get_index(0, vaddr);
	if (pml4[pml4_i] == 0) {
		log_info("✅ PML4 entry cleared — pruning successful");
	} else {
		log_error("❌ PML4 entry still set: 0x%lx", pml4[pml4_i]);
	}

	// 6. Cleanup
	free_page((void*)PHYS_TO_HHDM(paddr));
	_free_page_table(pml4);
}
