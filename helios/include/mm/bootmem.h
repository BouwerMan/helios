/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stddef.h>

#include <limine.h>

/**
 * @brief Initializes the bootmem memmory manager and mem_map.
 *
 * @param mmap Pointer to the Limine memory map response structure.
 */
void bootmem_init(struct limine_memmap_response* mmap);

/**
 * @brief Frees all pages managed by the boot allocator.
 *
 * @note: This function should only be called when the boot allocator is no longer needed.
 * @note: We assume that all pages allocated by the boot allocator are critical and should NEVER be deallocated.
 */
void bootmem_free_all();

/** 
 * @brief Reclaims memory marked as bootloader reclaimable.
 */
void bootmem_reclaim_bootloader();

/**
 * @brief Allocates a single page of physical memory.
 *
 * @return A pointer to the physical address of the allocated page, or NULL if
 *         no free pages are available.
 */
[[gnu::malloc, nodiscard]]
void* bootmem_alloc_page(void);

/**
 * @brief Allocates a contiguous block of physical memory pages.
 *
 * @param count The number of contiguous pages to allocate. Must be greater than 0.
 * @return A pointer to the starting address of the allocated block, or NULL if
 *         no suitable range is found.
 */
[[gnu::malloc, nodiscard]]
void* bootmem_alloc_contiguous(size_t count);

/**
 * @brief Frees a single page of physical memory.
 *
 * @param addr A pointer to the physical address of the page to be freed.
 */
void bootmem_free_page(void* addr);

/**
 * @brief Frees a contiguous block of physical memory pages.
 *
 * @param addr The starting address of the block to be freed. Must be page-aligned.
 * @param count The number of contiguous pages to free. Must be greater than 0.
 */
void bootmem_free_contiguous(void* addr, size_t count);

/**
 * @brief Checks if a physical page is marked as used in the boot allocator bitmap.
 *
 * @param phys_addr Physical address of the page to check.
 *
 * @returns true if the page is marked as used, false otherwise.
 */
bool bootmem_page_is_used(uintptr_t phys_addr);
