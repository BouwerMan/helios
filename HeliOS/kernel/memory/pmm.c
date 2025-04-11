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
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <util/log.h>

#ifndef __PMM_DEBUG__
#undef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

uint64_t bitmap_phys = 0; ///< Physical address of the bitmap.
uint64_t* bitmap = NULL;  ///< Pointer to the virtual memory mapped to bitmap_phys.
size_t bitmap_size = 0;   ///< Size of the bitmap in terms of uint64_t entries.

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
    size_t total_len = 0;   // Total length of usable memory.

    // First pass: Calculate the highest address and total usable memory length.
    for (size_t i = 0; i < mmap->entry_count; i++) {
        struct limine_memmap_entry* entry = mmap->entries[i];
        log_debug("%d. Start Addr: %x | Length: %x | Type: %d", i, entry->base, entry->length, entry->type);
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            high_addr = entry->base + entry->length;
            total_len += entry->length;
        }
    }

    // Calculate the size of the bitmap in bytes and entries.
    size_t bitmap_size_bytes = high_addr / PAGE_SIZE / 8; // 1 bit per page.
    bitmap_size = bitmap_size_bytes / sizeof(uint64_t);
    total_page_count = bitmap_size * 64;
    log_debug("Highest address: 0x%x, Total memory length: %x, requires a %d byte bitmap", high_addr, total_len,
              bitmap_size_bytes);

    // Second pass: Find a suitable location for the bitmap.
    for (size_t i = 0; i < mmap->entry_count; i++) {
        struct limine_memmap_entry* entry = mmap->entries[i];

        if (entry->type != LIMINE_MEMMAP_USABLE) continue;

        // Check if the memory region is large enough to hold the bitmap.
        if (entry->length > bitmap_size_bytes) {
            log_debug("Found valid location for bitmap at mmap entry: %d, base: 0x%x, length: %d", i, entry->base,
                      entry->length);

            bitmap_phys = align_up(entry->base);                     // Align the base address.
            bitmap = (uint64_t*)align_up(bitmap_phys + hhdm_offset); // Map to virtual memory.
            break;
        }
    }

    // Ensure a valid location for the bitmap was found.
    if (bitmap_phys == 0 || bitmap == NULL) {
        panic("Could not find valid location for memory bitmap");
    }

    log_debug("Located valid PMM bitmap location");
    log_debug("Putting bitmap at location: 0x%x, HHDM_OFFSET: 0x%x", bitmap, hhdm_offset);

    // Initialize the bitmap: Mark all pages as allocated.
    memset(bitmap, 0xFF, bitmap_size_bytes);

    // Third pass: Mark usable pages as free in the bitmap.
    for (size_t i = 0; i < mmap->entry_count; i++) {
        struct limine_memmap_entry* entry = mmap->entries[i];

        if (entry->type != LIMINE_MEMMAP_USABLE) continue;

        uint64_t start = align_up(entry->base);                 // Align the start address.
        uint64_t end = align_down(entry->base + entry->length); // Align the end address.

        for (uint64_t addr = start; addr < end; addr += PAGE_SIZE) {
            // Skip pages that overlap with the bitmap itself.
            if (addr >= bitmap_phys && addr < (bitmap_phys + bitmap_size_bytes)) continue;
            uint64_t page_index = addr / PAGE_SIZE; // Calculate the page index.
            uint64_t word_index = page_index / 64;  // Calculate the word index in the bitmap.
            uint64_t bit_offset = page_index % 64;  // Calculate the bit offset within the word.

            bitmap[word_index] &= ~(1ULL << bit_offset); // mark as free
            free_page_count++;
        }
    }

    log_info("PMM Initialized: %d free pages out of %d total pages", free_page_count, total_page_count);
}

void* pmm_alloc_page(void)
{
    free_page_count--;
    return NULL;
}

void pmm_free_page(void* addr)
{
    (void)addr;
    free_page_count++;
}

size_t pmm_get_free_page_count(void) { return free_page_count; }
size_t pmm_get_total_pages(void) { return total_page_count; }
