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

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/atomic.h>
#include <kernel/helios.h>
#include <kernel/sys.h>
#include <limine.h>
#include <mm/bootmem.h>
#include <mm/page.h>
#include <mm/page_alloc.h>
#include <util/log.h>

struct page* mem_map;

pfn_t max_pfn = 0;
const pfn_t min_pfn = 0; // Always 0
static size_t free_page_count;
static size_t total_page_count;

// Number of elements in bitmap
static size_t map_elems;
uint64_t* boot_bitmap = NULL;
#define BITSET_WIDTH (sizeof(uint64_t) * UINT8_WIDTH)

static inline uint64_t get_phys_addr(uint64_t word_offset, uint64_t bit_offset)
{
	return (word_offset * (BITSET_WIDTH * UINT8_WIDTH) + bit_offset) * PAGE_SIZE;
}

static inline uint64_t get_word_offset(uint64_t phys_addr)
{
	return (phys_addr / PAGE_SIZE) / BITSET_WIDTH;
}

static inline uint64_t get_bit_offset(uint64_t phys_addr)
{
	return (phys_addr / PAGE_SIZE) % BITSET_WIDTH;
}

void bootmem_init(struct limine_memmap_response* mmap)
{
	log_debug("Reading Memory Map");
	uint64_t high_addr = 0; // Highest address in the memory map.
	size_t total_len = 0;	// Total length of usable memory.
	uintptr_t boot_bitmap_phys = 0;

	// Example:  1. Start Addr: 52000 | Length: 4d000 | Type: 0

	// First pass: Calculate the highest address and total usable memory length.
	for (size_t i = 0; i < mmap->entry_count; i++) {
		struct limine_memmap_entry* entry = mmap->entries[i];
		log_debug("%zu. Start Addr: %lx | Length: %lx | Type: %lu", i, entry->base, entry->length, entry->type);
		if (entry->type != LIMINE_MEMMAP_USABLE) continue;
		high_addr = entry->base + entry->length;
		total_len += entry->length;
		uintptr_t start = align_up_page(entry->base);
		uintptr_t end = align_down_page(entry->base + entry->length);
		size_t first_pfn = start >> PAGE_SHIFT;
		size_t last_pfn = (end - PAGE_SIZE) >> PAGE_SHIFT;
		max_pfn = last_pfn;
	}

	// Calculate the size of the bitmap in bytes and entries.
	// size_t bitmap_size_bytes = high_addr / PAGE_SIZE / UINT8_WIDTH;
	// bitmap_size = bitmap_size_bytes / sizeof(uint64_t);
	// total_page_count = bitmap_size * BITSET_WIDTH;
	// log_debug("Highest address: 0x%lx, Total memory length: %zx, requires a %zu byte bitmap", high_addr, total_len,
	// 	  bitmap_size_bytes);

	size_t mapsize = ((max_pfn - min_pfn) + 7) / 8;
	map_elems = mapsize / sizeof(boot_bitmap[0]);
	log_debug("min_pfn: %lu, max_pfn: %lu, mapsize: %zu, map_elems: %zu", min_pfn, max_pfn, mapsize, map_elems);
	// Second pass: Find a suitable location for the bitmap.
	for (size_t i = 0; i < mmap->entry_count; i++) {
		struct limine_memmap_entry* entry = mmap->entries[i];

		if (entry->type != LIMINE_MEMMAP_USABLE) continue;

		// Check if the memory region is large enough to hold the bitmap.
		if (!(entry->length > mapsize)) continue;
		log_debug("Found valid location for bitmap at mmap entry: %zu, base: 0x%lx, length: %lu", i,
			  entry->base, entry->length);

		// Align the base address.
		boot_bitmap_phys = align_up_page(entry->base);
		// Map to virtual memory.
		boot_bitmap = (uint64_t*)(boot_bitmap_phys + HHDM_OFFSET);
		break;
	}

	// Ensure a valid location for the bitmap was found.
	if (boot_bitmap_phys == 0 || boot_bitmap == NULL) {
		panic("Could not find valid location for memory bitmap");
	}

	log_debug("Located valid PMM bitmap location");
	log_debug("Putting bitmap at location: 0x%lx, HHDM_OFFSET: 0x%lx", (uint64_t)boot_bitmap, HHDM_OFFSET);

	// Initialize the bitmap: Mark all pages as allocated.
	memset64(boot_bitmap, UINT64_MAX, map_elems);
	log_debug("boot_bitmap[123]: %lx", boot_bitmap[123]);

	// Third pass: Mark usable pages as free in the bitmap.
	for (size_t i = 0; i < mmap->entry_count; i++) {
		struct limine_memmap_entry* entry = mmap->entries[i];

		if (entry->type != LIMINE_MEMMAP_USABLE) continue;

		uint64_t start = align_up_page(entry->base);		     // Align the start address.
		uint64_t end = align_down_page(entry->base + entry->length); // Align the end address.

		for (uint64_t addr = start; addr < end; addr += PAGE_SIZE) {
			// Skip pages that overlap with the bitmap itself.
			if (addr >= boot_bitmap_phys && addr < (boot_bitmap_phys + mapsize)) continue;
			uint64_t page_index = addr / PAGE_SIZE;
			uint64_t word_index = page_index / BITSET_WIDTH;
			uint64_t bit_offset = page_index % BITSET_WIDTH;

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
	log_debug("Somehow allocated mem_map at: %p", (void*)mem_map);
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
			// pg->state = BLOCK_FREE;
		}
	}

#ifdef __KDEBUG__
	// This is a known reserved area, so we are just checking we set the flags right
	size_t test_pfn = boot_bitmap_phys >> PAGE_SHIFT;
	log_debug("Page %zu flags: %lx, ref_count: %d", test_pfn, mem_map[test_pfn].flags,
		  atomic_read(&mem_map[test_pfn].ref_count));
#endif
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
	if (addr == NULL) return;
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
	if (count == 0) {
		log_warn("count cannot be 0");
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

bool bootmem_page_is_used(uintptr_t phys_addr)
{
	uintptr_t page_index = phys_addr >> PAGE_SHIFT;
	uintptr_t word_offset = page_index / BITSET_WIDTH;
	uintptr_t bit_offset = page_index % BITSET_WIDTH;

	return ((boot_bitmap[word_offset] & (1ULL << bit_offset)) != 0);
}

// Free all pages managed by the boot allocator
void bootmem_free_all()
{
	// For testing, I will allocate a couple of pages in the bitmap, then send them to the buddy allocator
	// void* page1 = bootmem_alloc_page();
	// void* page2 = bootmem_alloc_page();
	// pfn_t pfn1 = phys_to_pfn((uintptr_t)page1);
	// pfn_t pfn2 = phys_to_pfn((uintptr_t)page2);
	//
	// log_debug("Freeing page %p (%zu) and %p (%zu)", page1, pfn1, page2, pfn2);
	//
	// __free_page(&mem_map[pfn1]);
	// __free_page(&mem_map[pfn2]);

	for (pfn_t pfn = min_pfn; pfn < max_pfn + min_pfn; pfn++) {
		uintptr_t page_phys = pfn_to_phys(pfn);
		if (bootmem_page_is_used(page_phys)) continue;
		struct page* page = &mem_map[pfn];

		clear_page_reserved(page);
		set_page_buddy(page);
		page->state = BLOCK_FREE;

		__free_page(page);
	}
	log_debug("Freed all old allocator memory");
	// for (size_t word = 0; word < map_elems; word++) {
	// 	// Skip fully used words in the bitmap
	// 	if (boot_bitmap[word] == UINT64_MAX) continue;
	//
	// 	for (size_t bit = 0; bit < BITSET_WIDTH; bit++) {
	// 		int used = boot_bitmap[word] & bit;
	// 		if (used == 0) continue;
	// 		phys_to_page()
	// 	}
	// 	// Find the first free bit in the word
	// 	int bit = __builtin_ffsll(~(int64_t)boot_bitmap[word]) - 1;
	// 	// bit can be -1 if ffsll is called with all allocated
	// 	if (bit < 0) continue;
	// 	boot_bitmap[word] |= (1ULL << bit); // Set page as used
	//
	// 	free_page_count--;
	// 	return (void*)get_phys_addr(word, (uint64_t)bit);
	// }
}
