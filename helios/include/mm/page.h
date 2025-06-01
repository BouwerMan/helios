/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stddef.h>

#include <kernel/types.h>
#include <util/list.h>

#if defined(__x86_64__)
#include <arch/x86_64/page.h>
#endif

#define PAGE_SHIFT 12
#define PAGE_SIZE  (1UL << PAGE_SHIFT)

#define HHDM_OFFSET 0xffff800000000000UL

#define PHYS_TO_HHDM(p) ((uintptr_t)(p) + HHDM_OFFSET)
#define HHDM_TO_PHYS(p) ((uintptr_t)(p) - HHDM_OFFSET)

#define PG_RESERVED (1UL << 0) // Is it reserved by an allocator?
#define PG_BUDDY    (1UL << 1) // FIXME: Probably not right

typedef size_t pfn_t;

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
	struct list list;
	atomic_t ref_count;  // Reference count for the page
	unsigned long flags; // Flags for the page (e.g., dirty, accessed)
	union {
		unsigned long private;

		// Buddy allocator stuff
		struct {
			uint8_t order;
			uint8_t state;
		};
	};
};

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
