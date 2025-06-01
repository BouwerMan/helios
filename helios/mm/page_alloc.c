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

#include <stdint.h>
#include <string.h>

#include <kernel/kmath.h>
#include <kernel/spinlock.h>
#include <mm/bootmem.h>
#include <mm/page.h>
#include <mm/page_alloc.h>

#ifndef __VMM_DEBUG__
#undef LOG_LEVEL
#define LOG_LEVEL 0
#define FORCE_LOG_REDEF
#include <util/log.h>
#undef FORCE_LOG_REDEF
#else
#include <util/log.h>
#endif

static struct free_area free_areas[MAX_ORDER];
static struct buddy_allocator alr = { 0 };

static void combine_blocks(struct buddy_allocator* allocator, struct page* page, size_t order);

void buddy_dump_free_lists()
{
	struct buddy_allocator* allocator = &alr;
	spinlock_acquire(&allocator->lock);

	for (size_t order = allocator->min_order; order <= allocator->max_order; order++) {
		struct list* head = &allocator->free_lists[order];

		if (list_empty(head)) {
			log_info("Order %zu: (empty)", order);
			continue;
		}

		log_info("Order %zu:", order);
		struct page* pg = NULL;
		list_for_each_entry(pg, head, list)
		{
			pfn_t pfn = page_to_pfn(pg);
			uintptr_t phys = pfn_to_phys(pfn);
			log_info("  -> pfn: 0x%lx, phys: 0x%lx", pfn, phys);
		}
	}

	spinlock_release(&allocator->lock);
}

void page_alloc_init()
{
	spinlock_init(&alr.lock);
	spinlock_acquire(&alr.lock);

	for (size_t i = 0; i <= MAX_ORDER; i++) {
		list_init(&alr.free_lists[i]);
	}
	alr.max_order = MAX_ORDER;
	alr.min_order = 0;

	spinlock_release(&alr.lock);

	bootmem_free_all();
}

static inline pfn_t get_buddy_idx(pfn_t pfn, size_t order)
{
	return pfn ^ (1UL << order);
}

#define LEFT_CHILD(pfn, order)	(pfn)
#define RIGHT_CHILD(pfn, order) ((pfn) + (1 << (order - 1)))

static struct page* split_until_order(struct buddy_allocator* allocator, struct page* page, size_t current_order,
				      size_t target_order)
{
	// Base case: if the current order matches the target, allocate the block
	if (current_order == target_order) {
		log_debug("Allocating");
		page->state = BLOCK_ALLOCATED;
		clear_page_buddy(page);
		return page;
	}
	pfn_t parent_pfn = page_to_pfn(page);
	log_debug("pfn: %zu, left child: %zu, right child: %zu", parent_pfn, LEFT_CHILD(parent_pfn, current_order),
		  RIGHT_CHILD(parent_pfn, current_order));

	// Split the block into two children
	struct page* left = &mem_map[parent_pfn];
	struct page* right = &mem_map[parent_pfn + (1 << (current_order - 1))];

	page->state = BLOCK_SPLIT;
	page->order = (uint8_t)current_order;

	left->state = BLOCK_SPLIT;
	left->order = (uint8_t)current_order - 1;

	right->state = BLOCK_FREE;
	right->order = (uint8_t)current_order - 1;

	// Add the right child to the free list
	log_debug("Inserting right child (pfn: %lx, order: %u) into free list", page_to_pfn(right), right->order);
	list_append(&allocator->free_lists[right->order], &right->list);

	log_debug("Split block pfn: %zu -> left pfn: %zu (%lx), right pfn: %zu (%lx)", parent_pfn, parent_pfn,
		  pfn_to_phys(parent_pfn), parent_pfn + (1 << (current_order - 1)),
		  pfn_to_phys(parent_pfn + (1 << (current_order - 1))));

	// We always recurse with the left child
	return split_until_order(allocator, left, left->order, target_order);
}

static struct page* __alloc_pages_core(struct buddy_allocator* allocator, flags_t flags, size_t order)
{
	(void)flags;
	if (order >= allocator->max_order) {
		log_error("Order: %zu, larger than max order: %zu", order, allocator->max_order);
		return NULL;
	}
	log_debug("Allocating pages with order: %zu", order);

	// Iterate from min requested order to largest possible
	for (size_t i = order; i <= allocator->max_order; i++) {
		struct list* order_list = &allocator->free_lists[i];
		log_debug("Trying order %zu free list", i);
		if (list_empty(order_list)) {
			log_debug("Free list for order %zu is empty", i);
			continue;
		}

		// Search for a free block in the current order list
		struct page* pg = NULL;
		list_for_each_entry(pg, order_list, list)
		{
			if (pg->state == BLOCK_FREE) {
				break;
			} else {
				// Since everything in this list should be free, going to go ahead and remove it
				log_warn(
					"Found non free block in free list with order: %zu, blockmeta_order: %u, blockmeta_state: %u",
					i, pg->order, pg->state);
				list_remove(&pg->list);
			}
		}

		// Ensure a valid free block was found
		if (!pg || pg->state != BLOCK_FREE) continue;

		log_debug("Found free block at pfn: %lx (order %u)", page_to_pfn(pg), pg->order);

		// Remove it from the list and mark it as split if needed
		list_remove(&pg->list);
		if (pg->order > order) pg->state = BLOCK_SPLIT;

		// Now we split recursively until we reach the desired order
		struct page* split_block = split_until_order(allocator, pg, pg->order, order);

		if (split_block) {
			log_debug("Successfully allocated block at pfn: %lx (order %zu)", page_to_pfn(split_block),
				  order);
		} else {
			log_warn("Failed to split block for order %zu", order);
		}

		return split_block;
	}
	return NULL;
}

struct page* alloc_page(flags_t flags)
{
	return alloc_pages(flags, 0);
}

struct page* alloc_pages(flags_t flags, size_t order)
{
	return __alloc_pages_core(&alr, flags, order);
}

uintptr_t __get_free_pages(flags_t flags, size_t order)
{
	struct page* pg = alloc_pages(flags, order);
	if (!pg) {
		log_error("Failed to allocate %zu pages with flags: %lx", 1UL << order, flags);
		return 0;
	}

	uintptr_t page_phys = page_to_phys(pg);
	return PHYS_TO_HHDM(page_phys);
}

uintptr_t __get_free_page(flags_t flags)
{
	return __get_free_pages(flags, 0);
}

uintptr_t get_free_pages(flags_t flags, size_t pages)
{
	size_t rounded_size = round_to_power_of_2(pages);
	size_t order = (size_t)log2(rounded_size);
	uintptr_t page_virt = __get_free_pages(flags, order);
	if (!page_virt) return 0;

	size_t region_size = PAGE_SIZE << order;
	memset((void*)page_virt, 0, region_size);

	return page_virt;
}

uintptr_t get_free_page(flags_t flags)
{
	return get_free_pages(flags, 0);
}

static void combine_blocks(struct buddy_allocator* allocator, struct page* page, size_t order)
{
	// First we mark the block as free

	size_t total_pages = 1UL << order;
	pfn_t init_pfn = page_to_pfn(page);

	// NOTE: I am just going to set the first page buddy for now, in the future I may have to mark more depending on (?)
	set_page_buddy(page);

	// // Go through each page and set PG_BUDDY so we know it is "free" and we contol it
	// // NOTE: No clue if this is required????
	// for (pfn_t pfn = init_pfn; pfn < init_pfn + total_pages; pfn++) {
	// 	struct page* pg = &mem_map[pfn];
	// 	set_page_buddy(pg);
	// }

	// Set the order of the first page of the block
	page->order = (uint8_t)order;
	page->state = BLOCK_FREE;

	list_append(&allocator->free_lists[order], &page->list);

	// log_debug("Found block at addr %lx with idx: %zu, current order %zu", pfn_to_phys(init_pfn), init_pfn, order);
	// If we are already at the highest order we have freed everything
	// NOTE: This HAS to come after the freeing above
	if (order >= allocator->max_order) {
		// log_debug("Reached map order (%zu) for page: %lx", order, init_pfn);
		return;
	}

	// Get the buddy
	pfn_t buddy_idx = get_buddy_idx(init_pfn, order);
	struct page* buddy = &mem_map[buddy_idx];
	// log_debug("Found buddy with pfn: %zu, state: %hhx, order: %hhx, next: %lx, prev: %lx", buddy_idx, buddy->state,
	// 	  buddy->order, (uintptr_t)buddy->list.next, (uintptr_t)buddy->list.prev);
	// Add to free list if needed (orphaned)
	// FIXME: This doesn't do what I think it does
	if ((buddy->state == BLOCK_INVALID) && page_buddy(buddy)) {
		log_debug("Buddy %zu not on free list for order %zu, adding...", buddy_idx, order);
		buddy->state = BLOCK_FREE;
		set_page_buddy(buddy);
		list_append(&allocator->free_lists[order], &buddy->list);
	}
	// Check if coalescing is possible
	if (buddy->state == BLOCK_FREE && buddy->order == page->order) {
		// log_debug("Coalescing block idx %zu with buddy idx %zu", init_pfn, buddy_idx);
		// Remove both blocks from the free lists and mark them as invalid
		list_remove(&page->list);
		list_remove(&buddy->list);
		page->state = BLOCK_INVALID;
		buddy->state = BLOCK_INVALID;

		// Only need to setup the order for the parent,
		// the first stage of this function will handle the rest during recursion
		// TODO: Document this
		size_t parent_pfn = init_pfn & ~((1 << (order + 1)) - 1);
		struct page* parent = &mem_map[parent_pfn];
		// log_debug("parent addr: %lx, parent idx: %zu, parent order: %u (size=%zu)", pfn_to_phys(parent_pfn),
		// 	  parent_pfn, parent->order, 1UL << (size_t)parent->order);
		combine_blocks(allocator, parent, order + 1);
	}
}

static void buddy_free(struct buddy_allocator* allocator, struct page* page, size_t order)
{
	spinlock_acquire(&allocator->lock);

	combine_blocks(allocator, page, order);

	spinlock_release(&allocator->lock);
}

void __free_pages(struct page* page, unsigned int order)
{
	if (!page) return;
	buddy_free(&alr, page, order);
}

void __free_page(struct page* page)
{
	__free_pages(page, 0);
}

void free_pages(void* addr, size_t pages)
{
	if (!addr) return;

	uintptr_t page_virt = HHDM_TO_PHYS((uintptr_t)addr);
	struct page* page = &mem_map[phys_to_pfn(page_virt)];

	size_t rounded_size = round_to_power_of_2(pages);
	size_t order = (size_t)log2(rounded_size);

	__free_pages(page, order);
}

void free_page(void* addr)
{
	free_pages(addr, 0);
}
