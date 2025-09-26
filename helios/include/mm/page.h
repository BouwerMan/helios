/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stddef.h>

#include "arch/atomic.h"
#include "kernel/assert.h"
#include "kernel/bitops.h"
#include "kernel/tasks/scheduler.h"
#include "kernel/types.h"
#include "lib/string.h"

static constexpr int PAGE_SHIFT = 12;
static constexpr size_t PAGE_SIZE = (1UL << PAGE_SHIFT);
static constexpr unsigned long PAGE_MASK = (~(PAGE_SIZE - 1));

static constexpr uintptr_t HHDM_OFFSET = 0xffff800000000000UL;

enum PG_FLAGS_BITS {
	PG_RESERVED_BIT,
	PG_BUDDY_BIT,
	PG_UPTODATE_BIT,
	PG_DIRTY_BIT,
	PG_LOCKED_BIT,
};

static constexpr flags_t PG_RESERVED = BIT(0);
static constexpr flags_t PG_BUDDY = BIT(1);
static constexpr flags_t PG_UPTODATE = BIT(2);
static constexpr flags_t PG_DIRTY = BIT(3);
static constexpr flags_t PG_LOCKED = BIT(4);
static constexpr flags_t PG_MAPPED = BIT(5);

extern struct page* mem_map;
extern pfn_t max_pfn;
extern const pfn_t min_pfn;

enum BLOCK_STATE {
	BLOCK_INVALID,
	BLOCK_FREE,
	BLOCK_SPLIT,
	BLOCK_ALLOCATED,
};

// TODO: Should do funky union shit so that I save space since this struct will serve as metadata for allocators
struct page {
	struct list_head list;

	/* Generic liveness. If it hits 0 the buddy allocator will recycle the frame */
	atomic_t ref_count;

	/* Number of pte mappings across all address spaces */
	atomic_t mapcount;

	// TODO: present_entries count for pt pruning

	flags_t flags;	     // Flags for the page (e.g., dirty, accessed)

	struct waitqueue wq; // Waitqueue for those waiting on PG_DIRTY

	union {
		unsigned long private;

		/* Buddy allocator stuff */
		struct {
			uint8_t order;
			uint8_t state;
		};

		/* File mapping */
		struct {
			struct inode_mapping* mapping;
			struct hlist_node map_node;
			pgoff_t index;
		};
	};
};

// TODO: Rework these into proper inline functions
#define PHYS_TO_HHDM(p) ((uintptr_t)(p) + HHDM_OFFSET)
#define HHDM_TO_PHYS(p) ((uintptr_t)(p) - HHDM_OFFSET)

[[gnu::always_inline]]
static inline bool is_page_aligned(uintptr_t addr)
{
	return (addr & (PAGE_SIZE - 1)) == 0;
}

static inline uintptr_t align_up_page(uintptr_t addr)
{
	return (addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}
static inline uintptr_t align_down_page(uintptr_t addr)
{
	return addr & ~(PAGE_SIZE - 1);
}

static inline pfn_t page_to_pfn(struct page* pg)
{
	return (pfn_t)(pg - mem_map);
}

static inline uintptr_t pfn_to_phys(pfn_t pfn)
{
	return pfn << PAGE_SHIFT;
}

static inline pfn_t phys_to_pfn(uintptr_t phys)
{
	return phys >> PAGE_SHIFT;
}

static inline struct page* phys_to_page(uintptr_t phys)
{
	return &mem_map[phys_to_pfn(phys)];
}

static inline uintptr_t page_to_phys(struct page* pg)
{
	if (pg) return pfn_to_phys(page_to_pfn(pg));
	return 0;
}

// Page flag functions
static inline void set_page_reserved(struct page* pg)
{
	if (pg) pg->flags |= PG_RESERVED;
}

static inline void clear_page_reserved(struct page* pg)
{
	if (pg) pg->flags &= ~PG_RESERVED;
}

static inline bool page_reserved(struct page* pg)
{
	if (pg) return ((pg->flags & PG_RESERVED) != 0);
	return false;
}

static inline void set_page_buddy(struct page* pg)
{
	if (pg) pg->flags |= PG_BUDDY;
}

static inline void clear_page_buddy(struct page* pg)
{
	if (pg) pg->flags &= ~PG_BUDDY;
}

static inline bool page_buddy(struct page* pg)
{
	if (pg) return ((pg->flags & PG_BUDDY) != 0);
	return false;
}

static inline struct page* get_page(struct page* pg)
{
	if (pg) atomic_inc(&pg->ref_count);
	return pg;
}

static inline struct page* map_page(struct page* pg)
{
	if (pg) atomic_inc(&pg->mapcount);
	return pg;
}

static inline struct page* unmap_page(struct page* pg)
{
	if (pg) atomic_dec(&pg->mapcount);
	return pg;
}

#include "mm/page_alloc.h" // For __free_page

static inline void put_page(struct page* pg)
{
	if (atomic_sub_and_test(1, &pg->ref_count)) {
		__free_page(pg);
	}
}

/**
 * pages_clear - Zero-fill a run of pages by virtual address
 * @start: Page-aligned start VA
 * @num_pages: Number of pages (>0)
 * Return: @start
 * Context: Does not sleep. IRQ-safe. Caller must ensure mapping writable.
 */
static inline void* pages_clear(void* start, size_t num_pages)
{
	kassert(is_page_aligned((uintptr_t)start),
		"pages_clear: pages not aligned");
	kassert(num_pages > 0, "pages_clear: num_pages must be > 0");
	if (!start) return start;
	return __memset(start, 0, num_pages << PAGE_SHIFT);
}

/**
 * page_clear - Zero-fill a single page by virtual address
 * @page: Page-aligned VA
 * Return: @page
 * Context: Does not sleep. IRQ-safe. Caller must ensure mapping writable.
 */
static inline void* page_clear(void* page)
{
	kassert(is_page_aligned((uintptr_t)page),
		"page_clear: page not aligned");
	if (!page) return page;
	return __memset(page, 0, PAGE_SIZE);
}

/**
 * __page_clear - Zero-fill a single physical page via HHDM
 * @page: Page descriptor
 * Return: none
 * Context: Does not sleep. IRQ-safe. Requires HHDM mapping of @page.
 */
static inline void __page_clear(struct page* page)
{
	if (!page) return;
	page_clear((void*)PHYS_TO_HHDM(page_to_phys(page)));
}

/**
 * __pages_clear - Zero-fill a run of physical pages via HHDM
 * @page: First page descriptor in the run
 * @num_pages: Number of pages (>0)
 * Return: none
 * Context: Does not sleep. IRQ-safe. Requires HHDM mapping of pages.
 */
static inline void __pages_clear(struct page* page, size_t num_pages)
{
	kassert(num_pages > 0, "__pages_clear: num_pages must be > 0");
	if (!page) return;
	pages_clear((void*)PHYS_TO_HHDM(page_to_phys(page)), num_pages);
}

bool trylock_page(struct page* page);

void lock_page(struct page* page);

bool tryunlock_page(struct page* page);

void unlock_page(struct page* page);

void wait_on_page_locked(struct page* page);
