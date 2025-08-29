/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

// NOTE: Trying out a new documentation style where I put the comments
// in here instead of the c file. Kind of like it since I can put large section headers

// This file is obviously very inspired by the linux kernel setup.

#include <stddef.h>
#include <stdint.h>

#include <kernel/helios.h>
#include <kernel/spinlock.h>
#include <lib/list.h>
#include <mm/page_alloc_flags.h>
#include <mm/zones.h>

static constexpr int MAX_ORDER = 10; // 2^10 pages (1024 pages), or 4MiB blocks

struct buddy_allocator {
	struct list_head free_lists[MAX_ORDER + 1]; // One for each order
	size_t size;				    // Total size in bytes
	size_t min_order;
	size_t max_order;
	spinlock_t lock;
};

/**
 * @brief Initializes the page allocator.
 */
void page_alloc_init();

void buddy_dump_free_lists();

/*******************************************************************************
* Allocation functions for the buddy allocator.
*
* Callers should be using get_free_page() or get_free_pages()
* for 99% of page allocations.
*******************************************************************************/

/**
 * @brief Allocates a contiguous block of pages, zeros them, and returns their virtual address.
 *
 * @param flags Allocation flags specifying memory constraints.
 * @param pages Number of pages to allocate.
 *
 * @return The virtual address of the first zeroed page, or 0 on failure.
 */
[[gnu::malloc, nodiscard]]
void* get_free_pages(aflags_t flags, size_t pages);

/**
 * @brief Allocates a contiguous block of pages.
 *
 * @param flags Allocation flags specifying memory constraints.
 * @param order Number of pages to allocate as a power of two (2^order).
 *
 * @return a pointer to the first page in the allocated block, or NULL on failure.
 */
[[nodiscard]]
struct page* alloc_pages(aflags_t flags, size_t order);

/**
 * @brief Allocates a single page, zeros it, and returns its virtual address.
 *
 * @param flags Allocation flags specifying memory constraints.
 *
 * @return the virtual address of the zeroed page, or 0 on failure.
 */
[[gnu::malloc, nodiscard, gnu::always_inline]]
static inline void* get_free_page(aflags_t flags)
{
	return get_free_pages(flags, 1);
}

/**
 * @brief Allocates a single page.
 *
 * @param flags Allocation flags specifying memory constraints.
 *
 * @return a pointer to the allocated page, or NULL on failure.
 */
[[nodiscard, gnu::always_inline]]
static inline struct page* alloc_page(aflags_t flags)
{
	return alloc_pages(flags, 0);
}

/**
 * @brief Allocates a contiguous block of pages and returns their virtual address.
 *
 * @param flags Allocation flags specifying memory constraints.
 * @param order Number of pages to allocate as a power of two (2^order).
 *
 * @note The allocated pages are not zeroed. The caller is responsible for
 *       initializing the memory if required.
 *
 * @return The virtual address of the first page in the allocated block, or 0
 *         if the allocation fails.
 */
[[gnu::malloc, nodiscard]]
void* __get_free_pages(aflags_t flags, size_t order);

/**
 * @brief Allocates a single page and returns its virtual address.
 *
 * @param flags Allocation flags specifying memory constraints.
 *
 * @note Does not zero the allocated page. Logs an error message if allocation fails.
 *
 * @return the virtual address of the page, or 0 on failure.
 */
[[gnu::malloc, nodiscard, gnu::always_inline]]
static inline void* __get_free_page(aflags_t flags)
{
	return __get_free_pages(flags, 0);
}

/*******************************************************************************
* Deallocation functions for the buddy allocator.
*
* Callers should be using free_page() or free_pages()
* for 99% of page deallocations.
*******************************************************************************/

/**
 * @brief Frees a block of pages from a virtual address.
 *
 * @param addr Virtual address of the first page in the block.
 * @param pages Number of pages to free.
 */
void free_pages(void* addr, size_t pages);

/**
 * @brief Frees a block of contiguous pages.
 *
 * @param page Pointer to the starting page of the block to be freed.
 * @param order The order of the block to be freed (size is 2^order pages).
 *
 * @note If the page is null or belongs to an invalid memory zone, the function
 *       logs an error and returns without performing any action.
 */
void __free_pages(struct page* page, size_t order);

/**
 * @brief Frees a single page from a virtual address.
 *
 * @param addr Virtual address of the page to be freed.
 */
[[gnu::always_inline]]
static inline void free_page(void* addr)
{
	free_pages(addr, 1);
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
