/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

// NOTE: Trying out a new documentation style where I put the comments
// in here instead of the c file. Kind of like it since I can put large section headers

// This file is obviously very inspired by the linux kernel setup.

// TODO: DMA allocations

#include <kernel/compiler_attributes.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/helios.h>
#include <kernel/spinlock.h>
#include <mm/page.h>
#include <util/list.h>

#include "page_alloc_internal.h"

#define MAX_ORDER 10 // 2^10 pages (1024 pages), or 4MiB blocks

struct buddy_allocator {
	struct list free_lists[MAX_ORDER + 1]; // One for each order
	size_t size;			       // Total size in bytes
	size_t min_order;
	size_t max_order;
	spinlock_t lock;
};

/**
 * @brief Initializes the page allocator.
 */
void page_alloc_init();

void buddy_dump_free_lists();

/**
* ============================================================
* Allocation functions for the buddy allocator.
* ============================================================
*
* Callers should be using get_free_page() or get_free_pages()
* for 99% of page allocations.
*/

/**
 * @brief Allocates a contiguous block of pages, zeros them, and returns their virtual address.
 *
 * @param flags Allocation flags specifying memory constraints.
 * @param order Number of pages to allocate.
 *
 * @return The virtual address of the first zeroed page, or 0 on failure.
 */
[[nodiscard]]
uintptr_t get_free_pages(flags_t flags, size_t pages);

/**
 * @brief Allocates a single page, zeros it, and returns its virtual address.
 *
 * @param flags Allocation flags specifying memory constraints.
 *
 * @return the virtual address of the zeroed page, or 0 on failure.
 */
[[nodiscard, gnu::always_inline]]
static inline uintptr_t get_free_page(flags_t flags)
{
	return get_free_pages(flags, 1);
}

/**
 * @brief Allocates a contiguous block of pages.
 *
 * @param flags Allocation flags specifying memory constraints.
 * @param order Number of pages to allocate as a power of two (2^order).
 *
 * @return a pointer to the first page in the allocated block, or NULL on failure.
 */
[[nodiscard, gnu::always_inline]]
static inline struct page* alloc_pages(flags_t flags, size_t order)

{
	return __alloc_pages_core(&alr, flags, order);
}

/**
 * @brief Allocates a single page.
 *
 * @param flags Allocation flags specifying memory constraints.
 *
 * @return a pointer to the allocated page, or NULL on failure.
 */
[[nodiscard, gnu::always_inline]]
static inline struct page* alloc_page(flags_t flags)
{
	return alloc_pages(flags, 0);
}

/**
 * @brief Allocates a contiguous block of pages and returns their virtual address.
 *
 * @param flags Allocation flags specifying memory constraints.
 * @param order Number of pages to allocate as a power of two (2^order).
 *
 * @note Does not zero the allocated pages. Logs an error message if allocation fails.
 *
 * @return the virtual address of the first page, or 0 on failure.
 */
[[nodiscard]]
uintptr_t __get_free_pages(flags_t flags, size_t order);

/**
 * @brief Allocates a single page and returns its virtual address.
 *
 * @param flags Allocation flags specifying memory constraints.
 *
 * @note Does not zero the allocated page. Logs an error message if allocation fails.
 *
 * @return the virtual address of the page, or 0 on failure.
 */
[[nodiscard, gnu::always_inline]]
static inline uintptr_t __get_free_page(flags_t flags)
{
	return __get_free_pages(flags, 0);
}

/**
* ============================================================
* Deallocation functions for the buddy allocator.
* ============================================================
*
* Callers should be using free_page() or free_pages()
* for 99% of page deallocations.
*/

/**
 * @brief Frees a block of pages from a virtual address.
 *
 * This function converts the virtual address to a physical address,
 * retrieves the corresponding page structure, and frees the block of pages.
 * It validates the input to ensure the address is not NULL.
 *
 * @param addr Virtual address of the first page in the block.
 * @param pages Number of pages to free.
 */
void free_pages(void* addr, size_t pages);

/**
 * @brief Frees a single page from a virtual address.
 *
 * @param addr Virtual address of the page to be freed.
 */
[[gnu::always_inline]]
static inline void free_page(void* addr)
{
	free_pages(addr, 0);
}

/**
 * @brief Frees a block of pages.
 *
 * @param page Pointer to the first page in the block to be freed.
 */
[[gnu::always_inline]]
static inline void __free_pages(struct page* page, size_t order)
{
	if (!page) return;
	__free_pages_core(&alr, page, order);
}

/**
 * @brief Frees a single page.
 *
 * @page: Pointer to the page to be freed.
 */
[[gnu::always_inline]]
static inline void __free_page(struct page* page)
{
	__free_pages(page, 0);
}
