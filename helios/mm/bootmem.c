/**
 * @file mm/bootmem.c
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

// This will be used to allocate memory before the buddy allocator is ready.

/**
* Initialization:
* 	1. We setup a bitmap similar to the old PMM. Where a 1 means a page is alloc and 0 means free.
* 	2. We find and allocate a space for mem_map. We then set this space as reserved.
* 		2a. Reserved will just be a linked list of struct page, these don't get released to the buddy allocator.
* 	I may end up just "freeing" pages which the bootmem allocator considers free.
* 	As long as we can assume anything allocated through this is critical, that should make things easier.
* 	So I don't actually have to "reserve" anything (though I may just set a flag in the struct page).
*/

#undef LOG_LEVEL
#define LOG_LEVEL 1
#define FORCE_LOG_REDEF
#include <util/log.h>
#undef FORCE_LOG_REDEF

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/atomic.h>
#include <kernel/bootinfo.h>
#include <kernel/helios.h>
#include <kernel/panic.h>
#include <limine.h>
#include <mm/bootmem.h>
#include <mm/page.h>
#include <mm/page_alloc.h>

struct page* mem_map;

pfn_t max_pfn = 0;
const pfn_t min_pfn = 0; // Always 0
static size_t free_page_count;
static size_t total_page_count;

// Number of elements in bitmap
static size_t map_elems;
static size_t bitmap_size;
static uintptr_t boot_bitmap_phys;
uint64_t* boot_bitmap = NULL;
#define BITSET_WIDTH (sizeof(uint64_t) * UINT8_WIDTH)

/**
 * @brief Calculates the physical address from word and bit offsets.
 *
 * @param word_offset Offset in the bitmap array (in words).
 * @param bit_offset Offset within the word (in bits).
 *
 * @return The physical address.
 */
[[gnu::always_inline]]
static inline uintptr_t get_phys_addr(size_t word_offset, size_t bit_offset)
{
	return (word_offset * BITSET_WIDTH + bit_offset) * PAGE_SIZE;
}

/**
 * @brief Calculates the page index from a physical address.
 *
 * @param phys_addr Physical address to calculate the page index for.
 *
 * @returns The page index.
 */
[[gnu::always_inline]]
static inline size_t get_page_index(uintptr_t phys_addr)
{
	return phys_addr >> PAGE_SHIFT;
}

/**
 * @brief Calculates the word offset in the bitmap from a physical address.
 *
 * @param phys_addr Physical address to calculate the word offset for.
 *
 * @returns The word offset.
 */
[[gnu::always_inline]]
static inline size_t get_word_offset(uintptr_t phys_addr)
{
	return get_page_index(phys_addr) / BITSET_WIDTH;
}

/**
 * @brief Calculates the bit offset within a word from a physical address.
 *
 * @param phys_addr Physical address to calculate the bit offset for.
 *
 * @return The bit offset.
 */
[[gnu::always_inline]]
static inline size_t get_bit_offset(uintptr_t phys_addr)
{
	return get_page_index(phys_addr) % BITSET_WIDTH;
}

/**
 * @brief Initializes the bootmem memmory manager and mem_map.
 *
 * @param mmap Pointer to the Limine memory map response structure.
 *
 * This function performs the following steps:
 * 1. Calculates the total usable memory and the highest physical address.
 * 2. Finds a suitable location for the PMM bitmap and maps it to virtual memory.
 * 3. Initializes the bitmap, marking all pages as allocated.
 * 4. Marks usable pages as free in the bitmap.
 * 5. Sets up the memory map structure to track page states.
 */
void bootmem_init(struct limine_memmap_response* mmap)
{
	log_debug("Reading Memory Map");
	size_t total_len = 0; // Total length of usable memory.

	// First pass: Calculate the highest address and total usable memory length.
	for (size_t i = 0; i < mmap->entry_count; i++) {
		struct limine_memmap_entry* entry = mmap->entries[i];
		log_debug("%zu. Start Addr: %lx | Length: %lx | Type: %lu", i, entry->base, entry->length, entry->type);
		if (entry->type != LIMINE_MEMMAP_USABLE) continue;

		/**
		 * NOTE: According to the limine protocol:
		 * The entries are guaranteed to be sorted by base address, lowest to highest.
		 * Usable and bootloader reclaimable entries are guaranteed to be 4096 byte aligned for both base and length.
		 */

		uintptr_t end = entry->base + entry->length;
		total_len += entry->length;
		max_pfn = (end - PAGE_SIZE) >> PAGE_SHIFT;
	}
	log_debug("Highest address: 0x%lx, Total memory length: %zx", pfn_to_phys(max_pfn + 1), total_len);

	bitmap_size = CEIL_DIV(max_pfn - min_pfn, (pfn_t)8);
	map_elems = bitmap_size / sizeof(boot_bitmap[0]);
	log_debug("min_pfn: %lu, max_pfn: %lu, mapsize: %zu, map_elems: %zu", min_pfn, max_pfn, bitmap_size, map_elems);

	// Second pass: Find a suitable location for the bitmap.
	for (size_t i = 0; i < mmap->entry_count; i++) {
		struct limine_memmap_entry* entry = mmap->entries[i];
		if (entry->type != LIMINE_MEMMAP_USABLE) continue;

		// Check if the memory region is large enough to hold the bitmap.
		if (!(entry->length > bitmap_size)) continue;
		log_debug("Found valid location for bitmap at mmap entry: %zu, base: 0x%lx, length: %lu", i,
			  entry->base, entry->length);

		boot_bitmap_phys = entry->base;
		// Map to virtual memory.
		boot_bitmap = (uint64_t*)PHYS_TO_HHDM(boot_bitmap_phys);
		break;
	}

	// Ensure a valid location for the bitmap was found.
	if (!boot_bitmap_phys || !boot_bitmap) {
		panic("Could not find valid location for memory bitmap");
	}

	log_debug("Located valid PMM bitmap location at: 0x%lx", boot_bitmap_phys);
	log_debug("Mapped bitmap to virtual memory at: %p", (void*)boot_bitmap);

	// Initialize the bitmap: Mark all pages as allocated.
	memset64(boot_bitmap, UINT64_MAX, map_elems);
	log_debug("boot_bitmap[123]: %lx", boot_bitmap[123]);

	// Third pass: Mark usable pages as free in the bitmap.
	for (size_t i = 0; i < mmap->entry_count; i++) {
		struct limine_memmap_entry* entry = mmap->entries[i];

		if (entry->type != LIMINE_MEMMAP_USABLE) continue;

		uint64_t start = entry->base;
		uint64_t end = entry->base + entry->length;

		for (uint64_t addr = start; addr < end; addr += PAGE_SIZE) {
			// Skip pages that overlap with the bitmap itself.
			if (addr >= boot_bitmap_phys && addr < (boot_bitmap_phys + bitmap_size)) continue;
			uint64_t word_index = get_word_offset(addr);
			uint64_t bit_offset = get_bit_offset(addr);

			// Mark as free
			boot_bitmap[word_index] &= ~(1ULL << bit_offset);
			free_page_count++;
		}
	}

	// TODO: Invert mem_map and bitmap init order, that way when we free the bitmap we have less fragmentation

	// Setup the mem_map
	total_page_count = max_pfn - min_pfn;
	size_t mem_map_size = total_page_count * sizeof(struct page);
	size_t req_pages = CEIL_DIV(mem_map_size, PAGE_SIZE);

	log_debug("mem_map_size: %zu, req_pages: %zu", mem_map_size, req_pages);
	mem_map = (void*)PHYS_TO_HHDM(bootmem_alloc_contiguous(req_pages));
	if (!mem_map) {
		panic("Could not allocate mem_map");
	}

	memset(mem_map, 0, mem_map_size);

	for (pfn_t pfn = 0; pfn < max_pfn; pfn++) {
		uintptr_t paddr = pfn_to_phys(pfn);
		struct page* pg = &mem_map[pfn];
		pg->flags = 0;
		if (bootmem_page_is_used(paddr)) {
			set_page_reserved(pg);
			atomic_set(&pg->ref_count, 1);
		} else { /* Page is free */
			atomic_set(&pg->ref_count, 0);
		}
	}
}

/**
 * @brief Allocates a single page of physical memory.
 *
 * This function scans the bitmap to find the first free page, marks it as used,
 * and returns its physical address. If no free pages are available, it logs a
 * warning and returns NULL.
 *
 * @return A pointer to the physical address of the allocated page, or NULL if
 *         no free pages are available.
 */
void* bootmem_alloc_page(void)
{
	if (!boot_bitmap) {
		log_error("boot_bitmap is not initialized or has already been decommissioned");
		return NULL;
	}

	for (size_t word = 0; word < map_elems; word++) {
		// Skip fully used words in the bitmap
		if (boot_bitmap[word] == UINT64_MAX) continue;
		// Find the first free bit in the word
		int bit = __builtin_ffsll(~(int64_t)boot_bitmap[word]) - 1;
		// bit can be -1 if ffsll is called with all allocated
		if (bit < 0) continue;
		boot_bitmap[word] |= (1ULL << bit); // Set page as used

		free_page_count--;
		return (void*)get_phys_addr(word, (uint64_t)bit);
	}
	log_warn("pmm_alloc_page: out of memory, no free pages left");
	return NULL;
}

/**
 * @brief Frees a single page of physical memory.
 *
 * This function marks the specified physical address as free in the bitmap
 * and increments the count of free pages. If the provided address is NULL,
 * the function returns immediately without performing any operation.
 *
 * @param addr A pointer to the physical address of the page to be freed.
 */
void bootmem_free_page(void* addr)
{
	if (!boot_bitmap) {
		log_error("boot_bitmap is not initialized or has already been decommissioned");
		return;
	}

	if (!addr) return;

	uint64_t phys_addr = (uint64_t)addr;
	uint64_t word_offset = get_word_offset(phys_addr);
	uint64_t bit_offset = get_bit_offset(phys_addr);
	boot_bitmap[word_offset] &= ~(1ULL << bit_offset);
	free_page_count++;
}

/**
 * @brief Allocates a contiguous block of physical memory pages.
 *
 * This function searches the bitmap for a contiguous range of free pages
 * that matches the requested count. If such a range is found, it marks
 * the pages as allocated in the bitmap and returns the starting address
 * of the allocated block. If no suitable range is found, it returns NULL.
 *
 * @param count The number of contiguous pages to allocate. Must be greater than 0.
 * @return A pointer to the starting address of the allocated block, or NULL if
 *         no suitable range is found.
 */
void* bootmem_alloc_contiguous(size_t count)
{
	if (!boot_bitmap) {
		log_error("boot_bitmap is not initialized or has already been decommissioned");
		return NULL;
	}

	if (count == 0) {
		log_error("count cannot be 0");
		return NULL;
	}

	size_t cont_start = SIZE_MAX;
	size_t cont_len = 0;
	for (size_t i = 0; i < total_page_count; i++) {
		uint64_t word_offset = i / BITSET_WIDTH;
		uint64_t bit_offset = i % BITSET_WIDTH;

		if ((boot_bitmap[word_offset] & (1ULL << bit_offset)) == 0) {
			if (cont_start == SIZE_MAX) cont_start = i;
			cont_len++;

			if (cont_len >= count) goto allocate_page;
		} else {
			cont_start = SIZE_MAX;
			cont_len = 0;
		}
	}
	log_warn("No valid contiguous range found for %zu pages", count);
	return NULL;

allocate_page:
	for (size_t i = cont_start; i < cont_start + count; i++) {
		uint64_t word_offset = i / BITSET_WIDTH;
		uint64_t bit_offset = i % BITSET_WIDTH;
		boot_bitmap[word_offset] |= (1ULL << bit_offset);
	}
	free_page_count -= count;
	return (void*)(cont_start * PAGE_SIZE);
}

/**
 * @brief Frees a contiguous block of physical memory pages.
 *
 * This function marks a range of pages as free in the bitmap, starting
 * from the given address and spanning the specified count of pages.
 * It ensures that the range being freed is within valid bounds and
 * updates the free page count accordingly.
 *
 * @param addr The starting address of the block to be freed. Must be page-aligned.
 * @param count The number of contiguous pages to free. Must be greater than 0.
 */
void bootmem_free_contiguous(void* addr, size_t count)
{
	if (!boot_bitmap) {
		log_error("boot_bitmap is not initialized or has already been decommissioned");
		return;
	}

	uint64_t page_index = (uint64_t)addr / PAGE_SIZE;
	if (page_index >= total_page_count) {
		log_error("Attempted to free page out of bounds: %lu", page_index);
		return;
	}
	uint64_t end_index = page_index + count;
	for (; page_index < end_index; page_index++) {
		uint64_t word_offset = page_index / BITSET_WIDTH;
		uint64_t bit_offset = page_index % BITSET_WIDTH;
		boot_bitmap[word_offset] &= ~(1ULL << bit_offset);
	}
	free_page_count += count;
}

/**
 * @brief Checks if a physical page is marked as used in the boot allocator bitmap.
 *
 * @param phys_addr Physical address of the page to check.
 *
 * This function determines whether a physical page is marked as used by inspecting
 * the corresponding bit in the boot allocator bitmap. The bitmap tracks the allocation
 * state of physical pages during the boot process.
 *
 * @returns true if the page is marked as used, false otherwise.
 */
bool bootmem_page_is_used(uintptr_t phys_addr)
{
	if (!boot_bitmap) {
		log_error("boot_bitmap is not initialized or has already been decommissioned");
		return true;
	}

	uintptr_t word_offset = get_word_offset(phys_addr);
	uintptr_t bit_offset = get_bit_offset(phys_addr);

	return ((boot_bitmap[word_offset] & (1ULL << bit_offset)) != 0);
}

/**
 * @brief Frees all pages managed by the boot allocator.
 *
 * This function decommissions the boot allocator by freeing all pages it manages.
 * It performs the following steps:
 * 1. Iterates through all physical pages tracked by the boot allocator and frees
 *    those that are not marked as used.
 * 2. Frees the memory used by the boot allocator bitmap itself.
 * 3. Logs the decommissioning process for debugging purposes.
 *
 * @note: This function should only be called when the boot allocator is no longer needed.
 * @note: We assume that all pages allocated by the boot allocator are critical and should NEVER be deallocated.
 */
void bootmem_free_all(void)
{
	if (!boot_bitmap) {
		log_error("boot_bitmap is not initialized or has already been decommissioned");
		return;
	}

	log_info("Decommissioning old bootmem allocator");

	// Free all pages not marked as used in the boot allocator bitmap.
	for (pfn_t pfn = min_pfn; pfn < max_pfn + min_pfn; pfn++) {
		uintptr_t page_phys = pfn_to_phys(pfn);
		if (bootmem_page_is_used(page_phys)) continue;
		struct page* page = &mem_map[pfn];

		clear_page_reserved(page);
		set_page_buddy(page);
		page->state = BLOCK_FREE;

		__free_page(page);
	}

	log_debug("Freed all old allocator memory, freeing bootmem bitmap");

	// Free the memory used by the boot allocator bitmap.
	for (uintptr_t phys = boot_bitmap_phys; phys < boot_bitmap_phys + bitmap_size; phys += PAGE_SIZE) {
		pfn_t pfn = phys_to_pfn(phys);
		struct page* page = &mem_map[pfn];

		clear_page_reserved(page);
		set_page_buddy(page);
		page->state = BLOCK_FREE;

		__free_page(page);
	}

	boot_bitmap = NULL;
	log_debug("Successfully decommissioned old bootmem allocator");
}

/**
 * @brief Reclaims memory marked as bootloader reclaimable.
 *
 * This function iterates through the memory map entries provided by the bootloader
 * and identifies regions marked as LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE. For each
 * such region, it processes the memory pages within the range, clears the reserved
 * flag, marks them as buddy pages, sets their state to BLOCK_FREE, and frees them
 * for use by the system.
 */
void bootmem_reclaim_bootloader()
{

	struct bootinfo* bootinfo = &kernel.bootinfo;
	if (!bootinfo->valid) {
		log_error("Invalid bootinfo");
		return;
	}

	size_t total_reclaimed = 0;

	for (size_t i = 0; i < bootinfo->memmap_entry_count; i++) {
		struct bootinfo_memmap_entry* entry = &bootinfo->memmap[i];
		if (entry->type != LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE) continue;

		uintptr_t start = entry->base;
		uintptr_t end = entry->base + entry->length;
		log_debug("Reclaimable range: start=0x%lx, end=0x%lx", start, end);
		for (size_t phys = start; phys < end; phys += PAGE_SIZE) {
			struct page* page = phys_to_page(phys);

			clear_page_reserved(page);
			set_page_buddy(page);
			page->state = BLOCK_FREE;

			__free_page(page);

			total_reclaimed += PAGE_SIZE;
		}
	}

	log_info("Reclaimed %zu KiB (%zu bytes) from the bootloader", total_reclaimed / 1024, total_reclaimed);
}
