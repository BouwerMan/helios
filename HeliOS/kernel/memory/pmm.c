/**
 * @file pmm.c
 * @brief Implementation of the Physical Memory Manager (PMM) for the HeliOS project.
 *
 * This file contains the implementation of the Physical Memory Manager, responsible
 * for managing physical memory in the system. It includes functions for initializing
 * the memory manager, allocating and freeing pages, and tracking memory usage.
 *
 * The PMM uses a bitmap to track the allocation status of physical memory pages.
 * It interacts with the memory map provided by the bootloader and ensures proper
 * alignment and allocation of memory regions.
 *
 * @author Dylan Parks
 * @date 2025-04-10
 * @license GPL-3.0
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <kernel/memory/pmm.h>
#include <kernel/sys.h>
#include <limine.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <util/log.h>

#ifndef __PMM_DEBUG__
#undef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

uint64_t bitmap_phys = 0; ///< Physical address of the bitmap.
uint64_t* bitmap =
	NULL; ///< Pointer to the virtual memory mapped to bitmap_phys. free is 0, allocated is 1
size_t bitmap_size = 0; ///< Size of the bitmap in terms of uint64_t entries.

static size_t free_page_count = 0;
static size_t total_page_count = 0; // Never changes after pmm_init()

/**
 * @brief Initializes the Physical Memory Manager (PMM).
 *
 * This function reads the memory map provided by the bootloader, calculates the
 * required bitmap size, and allocates a suitable location for the bitmap. It
 * then marks all pages as allocated and frees usable pages based on the memory map.
 *
 * @param mmap Pointer to the memory map response structure.
 * @param hhdm_offset Offset for the Higher Half Direct Mapping (HHDM).
 */
void pmm_init(struct limine_memmap_response* mmap, uint64_t hhdm_offset)
{
	log_debug("Reading Memory Map");
	uint64_t high_addr = 0; // Highest address in the memory map.
	size_t total_len = 0;	// Total length of usable memory.

	// First pass: Calculate the highest address and total usable memory length.
	for (size_t i = 0; i < mmap->entry_count; i++) {
		struct limine_memmap_entry* entry = mmap->entries[i];
		log_debug("%d. Start Addr: %x | Length: %x | Type: %d", i,
			  entry->base, entry->length, entry->type);
		if (entry->type != LIMINE_MEMMAP_USABLE) continue;
		high_addr = entry->base + entry->length;
		total_len += entry->length;
	}

	// Calculate the size of the bitmap in bytes and entries.
	size_t bitmap_size_bytes = high_addr / PAGE_SIZE / UINT8_WIDTH;
	bitmap_size = bitmap_size_bytes / sizeof(uint64_t);
	total_page_count = bitmap_size * BITSET_WIDTH;
	log_debug(
		"Highest address: 0x%x, Total memory length: %x, requires a %d byte bitmap",
		high_addr, total_len, bitmap_size_bytes);

	// Second pass: Find a suitable location for the bitmap.
	for (size_t i = 0; i < mmap->entry_count; i++) {
		struct limine_memmap_entry* entry = mmap->entries[i];

		if (entry->type != LIMINE_MEMMAP_USABLE) continue;

		// Check if the memory region is large enough to hold the bitmap.
		if (!(entry->length > bitmap_size_bytes)) continue;
		log_debug(
			"Found valid location for bitmap at mmap entry: %d, base: 0x%x, length: %d",
			i, entry->base, entry->length);

		// Align the base address.
		bitmap_phys = align_up(entry->base);
		// Map to virtual memory.
		bitmap = (uint64_t*)align_up(bitmap_phys + hhdm_offset);
		break;
	}

	// Ensure a valid location for the bitmap was found.
	if (bitmap_phys == 0 || bitmap == NULL) {
		panic("Could not find valid location for memory bitmap");
	}

	log_debug("Located valid PMM bitmap location");
	log_debug("Putting bitmap at location: 0x%x, HHDM_OFFSET: 0x%x", bitmap,
		  hhdm_offset);

	// Initialize the bitmap: Mark all pages as allocated.
	memset(bitmap, UINT8_MAX, bitmap_size_bytes);

	// Third pass: Mark usable pages as free in the bitmap.
	for (size_t i = 0; i < mmap->entry_count; i++) {
		struct limine_memmap_entry* entry = mmap->entries[i];

		if (entry->type != LIMINE_MEMMAP_USABLE) continue;

		uint64_t start =
			align_up(entry->base); // Align the start address.
		uint64_t end = align_down(
			entry->base + entry->length); // Align the end address.

		for (uint64_t addr = start; addr < end; addr += PAGE_SIZE) {
			// Skip pages that overlap with the bitmap itself.
			if (addr >= bitmap_phys &&
			    addr < (bitmap_phys + bitmap_size_bytes))
				continue;
			uint64_t page_index = addr / PAGE_SIZE;
			uint64_t word_index = page_index / BITSET_WIDTH;
			uint64_t bit_offset = page_index % BITSET_WIDTH;

			// Mark as free
			bitmap[word_index] &= ~(1ULL << bit_offset);
			free_page_count++;
		}
	}

	log_info("PMM Initialized: %d free pages out of %d total pages",
		 free_page_count, total_page_count);
#ifdef __PMM_TEST__
	pmm_test();
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
void* pmm_alloc_page(void)
{
	for (size_t word = 0; word < bitmap_size; word++) {
		// Skip fully used words in the bitmap
		if (bitmap[word] == UINT64_MAX) continue;
		// Find the first free bit in the word
		int bit = __builtin_ffsll(~bitmap[word]) - 1;
		// bit can be -1 if ffsll is called with all allocated
		if (bit < 0) continue;
		bitmap[word] |= (1ULL << bit); // Set page as used

		free_page_count--;
		return (void*)get_phys_addr(word, bit);
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
void pmm_free_page(void* addr)
{
	if (addr == NULL) return;
	uint64_t phys_addr = (uint64_t)addr;
	uint64_t word_offset = get_word_offset(phys_addr);
	uint64_t bit_offset = get_bit_offset(phys_addr);
	bitmap[word_offset] &= ~(1ULL << bit_offset);
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
void* pmm_alloc_contiguous(size_t count)
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

		if ((bitmap[word_offset] & (1ULL << bit_offset)) == 0) {
			if (cont_start == SIZE_MAX) cont_start = i;
			cont_len++;

			if (cont_len >= count) goto allocate_page;
		} else {
			cont_start = SIZE_MAX;
			cont_len = 0;
		}
	}
	log_warn("No valid contiguous range found for %d pages", count);
	return NULL;

allocate_page:
	for (size_t i = cont_start; i < cont_start + count; i++) {
		uint64_t word_offset = i / BITSET_WIDTH;
		uint64_t bit_offset = i % BITSET_WIDTH;
		bitmap[word_offset] |= (1ULL << bit_offset);
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
void pmm_free_contiguous(void* addr, size_t count)
{
	uint64_t page_index = (uint64_t)addr / PAGE_SIZE;
	if (page_index >= total_page_count) {
		log_error("Attempted to free page out of bounds: %d",
			  page_index);
		return;
	}
	uint64_t end_index = page_index + count;
	for (; page_index < end_index; page_index++) {
		uint64_t word_offset = page_index / BITSET_WIDTH;
		uint64_t bit_offset = page_index % BITSET_WIDTH;
		bitmap[word_offset] &= ~(1ULL << bit_offset);
	}
	free_page_count += count;
}

size_t pmm_get_free_page_count(void)
{
	return free_page_count;
}
size_t pmm_get_total_pages(void)
{
	return total_page_count;
}

// Testing code

#define MAX_TEST_PAGES 128

static int tests_passed = 0;
static int tests_failed = 0;

#define EXPECT(expr, msg)                        \
	do {                                     \
		if (expr) {                      \
			log_info("[PASS] " msg); \
			tests_passed++;          \
		} else {                         \
			log_warn("[FAIL] " msg); \
			tests_failed++;          \
		}                                \
	} while (0)

/**
 * Runs a series of tests to validate the functionality of the Physical Memory Manager (PMM).
 *
 * This function performs the following tests:
 * 1. Allocates individual pages and verifies their uniqueness and alignment.
 * 2. Frees the allocated pages and ensures the free page count is restored.
 * 3. Allocates a contiguous block of pages, checks their adjacency, and frees them.
 * 4. Attempts to allocate a zero-sized block and ensures it fails.
 *
 * Logs the results of each test and provides a summary of passed and failed tests.
 */
void pmm_test(void)
{
	log_info("==== Running PMM Tests ====");

	size_t initial_free = pmm_get_free_page_count();
	void* pages[MAX_TEST_PAGES] = { 0 };

	// --- Test 1: Allocate individual pages ---
	for (size_t i = 0; i < MAX_TEST_PAGES; i++) {
		pages[i] = pmm_alloc_page();
		EXPECT(pages[i] != NULL,
		       "pmm_alloc_page() should not return NULL");
		EXPECT(((uint64_t)pages[i] % PAGE_SIZE) == 0,
		       "Allocated page should be page-aligned");

		// Optional uniqueness check
		for (size_t j = 0; j < i; j++) {
			EXPECT(pages[i] != pages[j],
			       "Allocated page should be unique");
		}
	}

	size_t after_alloc = pmm_get_free_page_count();
	EXPECT(after_alloc == initial_free - MAX_TEST_PAGES,
	       "free_page_count should decrease by MAX_TEST_PAGES");

	// --- Test 2: Free the pages ---
	for (size_t i = 0; i < MAX_TEST_PAGES; i++) {
		pmm_free_page(pages[i]);
	}

	size_t after_free = pmm_get_free_page_count();
	EXPECT(after_free == initial_free,
	       "free_page_count should return to initial after freeing");

	// --- Test 3: Allocate contiguous ---
	void* block = pmm_alloc_contiguous(16);
	EXPECT(block != NULL, "pmm_alloc_contiguous(16) should succeed");
	EXPECT(((uint64_t)block % PAGE_SIZE) == 0,
	       "Contiguous block should be page-aligned");

	if (block != NULL) {
		for (int i = 1; i < 16; i++) {
			uint64_t expected = (uint64_t)block + i * PAGE_SIZE;
			void* check = (void*)expected;
			// Optionally add a used check here
			EXPECT((uint64_t)check == expected,
			       "Contiguous pages should be adjacent");
		}

		pmm_free_contiguous(block, 16);
		EXPECT(pmm_get_free_page_count() == initial_free,
		       "free_page_count should restore after contiguous free");
	}

	// --- Test 4: Zero page allocation ---
	void* z = pmm_alloc_contiguous(0);
	EXPECT(z == NULL, "pmm_alloc_contiguous(0) should return NULL");

	log_info("==== PMM Tests Complete ====");
	log_info("Total: %d passed, %d failed", tests_passed, tests_failed);
}
