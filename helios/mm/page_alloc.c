/**
 * @file mm/page_alloc.c
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

// This should be the main PMM buddy allocator
// Heavily based on https://www.kernel.org/doc/gorman/html/understand/understand009.html
// I am being lazy so I am technically assuming UMA, and currently I don't have support for zones such as DMA

// TODO: When freeing we change the order of the free list (we append always).
// This does mean that consecutive allocs and frees may give different results.
// Maybe I should make it not do thet?

// FIXME: We never use anything other than BLOCK_FREE and BLOCK_ALLOCATED.
// And we only check for BLOCK_FREE.

#undef LOG_LEVEL
#define LOG_LEVEL 1
#define FORCE_LOG_REDEF
#include <util/log.h>
#undef FORCE_LOG_REDEF

#include <kernel/helios.h>
#include <kernel/kmath.h>
#include <kernel/panic.h>
#include <kernel/spinlock.h>
#include <mm/address_space.h>
#include <mm/bootmem.h>
#include <mm/page.h>
#include <mm/page_alloc.h>
#include <stdint.h>
#include <string.h>

/*******************************************************************************
* Global Variable Definitions
*******************************************************************************/

struct buddy_allocator norm_alr = { 0 };
struct buddy_allocator dma32_alr = { 0 };
struct buddy_allocator dma_alr = { 0 };

// NOTE: These must match the order of enum MEM_ZONE members

/// Lookup table for buddy allocators based on memory zones
static struct buddy_allocator* regions[] = {
	[MEM_ZONE_DMA] = &dma_alr,
	[MEM_ZONE_DMA32] = &dma32_alr,
	[MEM_ZONE_NORMAL] = &norm_alr,
};

/*******************************************************************************
* Private Function Prototypes
*******************************************************************************/

/**
 * @brief Initializes a buddy allocator structure.
 *
 * @param allocator Pointer to the buddy allocator to be initialized.
 */
static void allocator_init(struct buddy_allocator* allocator);

/**
 * @brief Recursively splits a memory block until it reaches the target order.
 *
 * @param allocator Pointer to the buddy allocator structure.
 * @param page Pointer to the page representing the current memory block.
 * @param current_order The current order of the memory block.
 * @param target_order The desired order to split the block down to.
 *
 * @return A pointer to the page representing the allocated block at the target order.
 */
static struct page* split_until_order(struct buddy_allocator* allocator,
				      struct page* page,
				      size_t current_order,
				      size_t target_order);

/**
 * @brief Allocates pages from the buddy allocator.
 *
 * @param allocator Pointer to the buddy allocator structure.
 * @param flags Allocation flags (currently unused).
 * @param order The order of the pages to allocate.
 *
 * @return A pointer to the allocated page structure, or NULL if allocation fails.
 */
static struct page* alloc_pages_core(struct buddy_allocator* allocator,
				     aflags_t flags,
				     size_t order);

/**
 * @brief Coalesces free memory blocks into larger blocks.
 *
 * @param allocator Pointer to the buddy allocator structure.
 * @param page Pointer to the page structure representing the current block.
 * @param order The order of the current block.
 */
static void combine_blocks(struct buddy_allocator* allocator,
			   struct page* page,
			   size_t order);

/**
 * @brief Frees pages back to the buddy allocator.
 *
 * @param allocator Pointer to the buddy allocator structure.
 * @param page Pointer to the page structure representing the block to free.
 * @param order The order of the block being freed.
 */
static void free_pages_core(struct buddy_allocator* allocator,
			    struct page* page,
			    size_t order);

/**
 * @brief Determine the memory zone of a given page.
 *
 * @param pg Pointer to the page structure.
 * @return An enum value representing the memory zone of the page.
 */
static inline enum MEM_ZONE page_zone(struct page* pg)
{
	uintptr_t phys = page_to_phys(pg);

	if (phys < ZONE_DMA_LIMIT) {
		return MEM_ZONE_DMA;
	} else if (phys >= ZONE_DMA32_BASE && phys < ZONE_DMA32_LIMIT) {
		return MEM_ZONE_DMA32;
	} else if (phys >= ZONE_NORMAL_BASE && phys < ZONE_NORMAL_LIMIT) {
		return MEM_ZONE_NORMAL;
	}

	return MEM_ZONE_INVALID;
}

[[gnu::always_inline]]
static inline pfn_t parent_pfn(pfn_t pfn, size_t order)
{
	return pfn & ~((1UL << (order + 1UL)) - 1UL);
}

[[gnu::always_inline]]
static inline pfn_t left_child_pfn(pfn_t pfn, size_t order)
{
	(void)order;
	return pfn;
}

[[gnu::always_inline]]
static inline pfn_t right_child_pfn(pfn_t pfn, size_t order)
{
	return pfn + (1UL << (order - 1UL));
}

[[gnu::always_inline]]
static inline pfn_t buddy_pfn(pfn_t pfn, size_t order)
{
	return pfn ^ (1UL << order);
}

/*******************************************************************************
* Public Function Definitions
*******************************************************************************/

void page_alloc_init()
{
	allocator_init(&dma_alr);
	allocator_init(&dma32_alr);
	allocator_init(&norm_alr);

	bootmem_free_all();

	address_space_init();
}

void buddy_dump_free_lists()
{
	struct buddy_allocator* allocator = &norm_alr;
	spinlock_acquire(&allocator->lock);

	for (size_t order = allocator->min_order; order <= allocator->max_order;
	     order++) {
		struct list_head* head = &allocator->free_lists[order];

		if (list_empty(head)) {
			log_info("Order %zu: (empty)", order);
			continue;
		}

		log_info("Order %zu:", order);
		struct page* pg = NULL;
		list_for_each_entry (pg, head, list) {
			pfn_t pfn = page_to_pfn(pg);
			uintptr_t phys = pfn_to_phys(pfn);
			log_info("  -> pfn: 0x%lx, phys: 0x%lx", pfn, phys);
		}
	}

	spinlock_release(&allocator->lock);
}

/**
 * @brief Allocates a contiguous block of memory pages.
 *
 * @param flags Allocation flags specifying memory constraints.
 * @param order Number of pages to allocate as a power of two (2^order).
 *
 * This function attempts to allocate a block of memory pages from the
 * specified memory zone based on the provided flags. It iterates through
 * memory zones starting from the one specified by the flags and attempts
 * to allocate the requested pages. If successful, it returns a pointer
 * to the allocated page structure; otherwise, it returns NULL.
 *
 * @return Pointer to the first page in the allocated block, or NULL on failure.
 */
[[nodiscard]]
struct page* alloc_pages(aflags_t flags, size_t order)
{
	struct page* pg = NULL;

	static const size_t flag_to_zone[] = {
		[AF_DMA] = MEM_ZONE_DMA,
		[AF_DMA32] = MEM_ZONE_DMA32,
		[AF_NORMAL] = MEM_ZONE_NORMAL,
	};

	aflags_t zone_flags = flags & ZONE_MASK;
	size_t region_index = (zone_flags < ARRAY_SIZE(flag_to_zone)) ?
				      flag_to_zone[zone_flags] :
				      MEM_ZONE_INVALID;
	log_debug(
		"zone_flags: %x, region_index: %zu, flag_to_zone[zone_flags]: %zu",
		zone_flags,
		region_index,
		flag_to_zone[zone_flags]);

	if (region_index == MEM_ZONE_INVALID) {
		log_error("Invalid allocation flags: %x", flags);
		return NULL;
	}

	for (; region_index < MEM_NUM_ZONES; region_index--) {
		log_debug("Trying to allocate from region: %zu", region_index);
		pg = alloc_pages_core(regions[region_index], flags, order);
		if (pg) {
			log_debug("Allocated page at %p with order: %zu",
				  (void*)page_to_phys(pg),
				  order);

			if (atomic_read(&pg->ref_count) >= 1) {
				log_warn("page has refcount of %d",
					 atomic_read(&pg->ref_count));
			}

			atomic_set(&pg->ref_count, 1);
			break;
		}
	}

	return pg;
}

/**
 * @brief Allocates a contiguous block of pages and returns their virtual address.
 *
 * This function attempts to allocate a block of contiguous memory pages based on
 * the specified allocation flags and order. The order determines the size of the
 * allocation as a power of two (2^order). If the allocation fails, an error is
 * logged, and the function returns 0.
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
void* __get_free_pages(aflags_t flags, size_t order)
{
	struct page* pg = alloc_pages(flags, order);
	if (!pg) {
		log_error("Failed to allocate %zu pages with flags: %x",
			  1UL << order,
			  flags);
		return 0;
	}

	uintptr_t page_phys = page_to_phys(pg);
	return (void*)PHYS_TO_HHDM(page_phys);
}

/**
 * @brief Allocates a contiguous block of pages, zeros them, and returns their virtual address.
 *
 * This function allocates a block of contiguous memory pages based on the specified
 * allocation flags and the number of pages requested. The allocated memory is zeroed
 * before being returned to the caller. If the allocation fails, the function returns 0.
 *
 * @param flags Allocation flags specifying memory constraints.
 * @param pages Number of pages to allocate.
 *
 * @return The virtual address of the first zeroed page, or 0 on failure.
 */
void* get_free_pages(aflags_t flags, size_t pages)
{
	size_t rounded_size = roundup_pow_of_two(pages);
	size_t order = (size_t)ilog2(rounded_size);
	void* page_virt = __get_free_pages(flags, order);
	if (!page_virt) return 0;

	size_t region_size = PAGE_SIZE << order;
	memset64((uint64_t*)page_virt, 0, region_size / sizeof(uint64_t));

	return page_virt;
}

/**
 * @brief Frees a block of contiguous pages.
 *
 * This function releases a previously allocated block of contiguous memory pages
 * back to the memory allocator. The block is identified by the starting page and
 * the order, which determines the size of the block as a power of two (2^order).
 *
 * @param page Pointer to the starting page of the block to be freed.
 * @param order The order of the block to be freed (size is 2^order pages).
 *
 * @note If the page is null or belongs to an invalid memory zone, the function
 *       logs an error and returns without performing any action.
 */
void __free_pages(struct page* page, size_t order)
{
	if (!page) return;

	kassert(atomic_read(&page->ref_count) == 0);

	enum MEM_ZONE zone = page_zone(page);
	if (zone == MEM_ZONE_INVALID) {
		log_error("Invalid page zone for page at %p", (void*)page);
		return;
	}

	free_pages_core(regions[zone], page, order);
}

/**
 * @brief Frees a specified number of pages starting at a given address.
 *
 * @param addr The virtual address of the first page to free.
 * @param pages The number of pages to free.
 *
 * This function releases memory pages back to the system. It calculates
 * the physical address corresponding to the virtual address, determines
 * the page structure, and computes the order of pages to free based on
 * the rounded size. The pages are then freed using the __free_pages function.
 *
 * If the provided address is NULL, the function returns immediately without
 * performing any operations.
 */
void free_pages(void* addr, size_t pages)
{
	if (!addr) return;
	if ((uintptr_t)addr & ~PAGE_MASK) {
		log_error("Address %p is not page-aligned", addr);
		return;
	}

	uintptr_t page_virt = HHDM_TO_PHYS((uintptr_t)addr);
	struct page* page = &mem_map[phys_to_pfn(page_virt)];

	if (atomic_sub_and_test(1, &page->ref_count)) {
		size_t rounded_size = roundup_pow_of_two(pages);
		size_t order = (size_t)ilog2(rounded_size);

		log_debug("Freeing %zu pages at address %p (order: %zu)",
			  pages,
			  addr,
			  order);
		__free_pages(page, order);
	}
}

/*******************************************************************************
* Private Function Definitions
*******************************************************************************/

static void allocator_init(struct buddy_allocator* allocator)
{
	spinlock_init(&allocator->lock);
	spinlock_acquire(&allocator->lock);

	for (size_t order = 0; order <= MAX_ORDER; order++) {
		list_init(&allocator->free_lists[order]);
	}
	allocator->max_order = MAX_ORDER;
	allocator->min_order = 0;

	spinlock_release(&allocator->lock);
}

/**
 * @brief Recursively splits a memory block until it reaches the target order.
 *
 * @param allocator Pointer to the buddy allocator structure.
 * @param page Pointer to the page representing the current memory block.
 * @param current_order The current order of the memory block.
 * @param target_order The desired order to split the block down to.
 *
 * This function recursively splits a memory block into smaller blocks until the block
 * reaches the desired order. It updates the state and order of the parent and child blocks,
 * adds the right child to the free list, and always recurses with the left child.
 *
 * @return A pointer to the page representing the allocated block at the target order.
 */
static struct page* split_until_order(struct buddy_allocator* allocator,
				      struct page* page,
				      size_t current_order,
				      size_t target_order)
{
	// Base case: if the current order matches the target, allocate the block
	if (current_order == target_order) {
		log_debug("Allocating");
		page->state = BLOCK_ALLOCATED;
		clear_page_buddy(page);
		return page;
	}

	pfn_t prnt_pfn = page_to_pfn(page);
	pfn_t left_pfn = left_child_pfn(prnt_pfn, current_order);
	pfn_t right_pfn = right_child_pfn(prnt_pfn, current_order);
	log_debug(
		"Splitting block: parent pfn: %zu, left pfn: %zu, right pfn: %zu",
		prnt_pfn,
		left_pfn,
		right_pfn);

	// Split the block into two children
	struct page* left = &mem_map[left_pfn];
	struct page* right = &mem_map[right_pfn];

	// Update states and orders
	page->state = BLOCK_SPLIT;
	page->order = (uint8_t)current_order;

	left->state = BLOCK_SPLIT;
	left->order = (uint8_t)(current_order - 1);

	right->state = BLOCK_FREE;
	right->order = (uint8_t)(current_order - 1);

	// Add the right child to the free list
	list_append(&allocator->free_lists[right->order], &right->list);

	log_debug(
		"Split block pfn: %zu -> left pfn: %zu (%lx), right pfn: %zu (%lx)",
		prnt_pfn,
		left_pfn,
		pfn_to_phys(left_pfn),
		right_pfn,
		pfn_to_phys(right_pfn));

	// We always recurse with the left child
	return split_until_order(allocator, left, left->order, target_order);
}

/**
 * @brief Allocates pages from the buddy allocator.
 *
 * @param allocator Pointer to the buddy allocator structure.
 * @param flags Allocation flags (currently unused).
 * @param order The order of the pages to allocate.
 *
 * This function attempts to allocate pages of the specified order from the buddy allocator.
 * It iterates through the free lists starting from the requested order up to the maximum order.
 * If a free block is found, it is either allocated directly or split recursively to match the
 * desired order. Invalid blocks are removed from the free list to maintain consistency.
 *
 * @return A pointer to the allocated page structure, or NULL if allocation fails.
 */
static struct page* alloc_pages_core(struct buddy_allocator* allocator,
				     aflags_t flags,
				     size_t order)
{
	(void)flags;
	if (order >= allocator->max_order) {
		log_error("Order: %zu, larger than max order: %zu",
			  order,
			  allocator->max_order);
		return nullptr;
	}
	log_debug("Allocating pages with order: %zu", order);

	// Iterate from requested order to largest possible
	for (size_t i = order; i <= allocator->max_order; i++) {
		struct list_head* order_list = &allocator->free_lists[i];
		if (list_empty(order_list)) {
			log_debug("Free list for order %zu is empty", i);
			continue;
		}

		// Search for a free block in the current order list
		struct page* pg = NULL;
		list_for_each_entry (pg, order_list, list) {
			if (pg->state == BLOCK_FREE) {
				break;
			} else {
				// Since everything in this list should be free, going to go ahead and remove it
				log_warn(
					"Found non free block in free list with order: %zu, blockmeta_order: %u, blockmeta_state: %u",
					i,
					pg->order,
					pg->state);
				list_remove(&pg->list);
			}
		}

		// Ensure a valid free block was found
		if (!pg || pg->state != BLOCK_FREE) continue;

		log_debug("Found free block at pfn: %lx (order %u)",
			  page_to_pfn(pg),
			  pg->order);

		// Remove it from the list
		list_remove(&pg->list);

		// Now we split recursively until we reach the desired order
		struct page* split_block =
			split_until_order(allocator, pg, pg->order, order);

		if (split_block) {
			log_debug(
				"Successfully allocated block at pfn: %lx (order %zu)",
				page_to_pfn(split_block),
				order);
		} else {
			log_error("Failed to split block for order %zu", order);
		}

		return split_block;
	}
	return nullptr;
}

/**
 * @brief Coalesces free memory blocks into larger blocks.
 *
 * @param allocator Pointer to the buddy allocator structure.
 * @param page Pointer to the page structure representing the current block.
 * @param order The order of the current block.
 *
 * This function attempts to combine adjacent free blocks of the same order
 * into a larger block of the next order. It recursively continues this process
 * until no further coalescing is possible or the maximum order is reached.
 *
 * Steps:
 * 1. Mark the current block as free and add it to the free list.
 * 2. Check if the buddy block is free and of the same order.
 * 3. If coalescing is possible, remove both blocks from the free list,
 *    mark them as invalid, and recursively combine them into a parent block.
 */
static void combine_blocks(struct buddy_allocator* allocator,
			   struct page* page,
			   size_t order)
{
	// Mark the block as free and initialize its state
	pfn_t init_pfn = page_to_pfn(page);
	set_page_buddy(page);
	page->order = (uint8_t)order;
	page->state = BLOCK_FREE;

	// Add the block to the free list
	list_append(&allocator->free_lists[order], &page->list);

	// If we are already at the highest order we have freed everything
	// NOTE: This HAS to come after the freeing above
	if (order >= allocator->max_order) {
		return;
	}

	// Get the buddy
	pfn_t bdy_pfn = buddy_pfn(init_pfn, order);
	struct page* buddy = &mem_map[bdy_pfn];

	// Check if coalescing is possible
	if (buddy->state == BLOCK_FREE && buddy->order == page->order) {
		// Remove both blocks from the free lists and mark them as invalid
		list_remove(&page->list);
		list_remove(&buddy->list);
		page->state = BLOCK_INVALID;
		buddy->state = BLOCK_INVALID;

		size_t prnt_pfn = parent_pfn(init_pfn, order);
		struct page* parent = &mem_map[prnt_pfn];

		combine_blocks(allocator, parent, order + 1);
	}
}

/**
 * @brief Frees pages back to the buddy allocator.
 *
 * @param allocator Pointer to the buddy allocator structure.
 * @param page Pointer to the page structure representing the block to free.
 * @param order The order of the block being freed.
 *
 * This function frees a block of memory back to the buddy allocator. It acquires
 * the allocator's spinlock to ensure thread safety, combines adjacent free blocks
 * to maintain the buddy system's structure, and then releases the spinlock.
 */
static void free_pages_core(struct buddy_allocator* allocator,
			    struct page* page,
			    size_t order)
{
	spinlock_acquire(&allocator->lock);

	combine_blocks(allocator, page, order);

	spinlock_release(&allocator->lock);
}
