/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stddef.h>

#include <kernel/types.h>
#include <util/list.h>

#define PAGE_SHIFT 12
#define PAGE_SIZE  (1UL << PAGE_SHIFT)

#define HHDM_OFFSET 0xffff800000000000UL

#define PHYS_TO_HHDM(p) ((uintptr_t)(p) + HHDM_OFFSET)
#define HHDM_TO_PHYS(p) ((uintptr_t)(p) - HHDM_OFFSET)

typedef size_t pfn_t;

#define PG_RESERVED (1UL << 0)

struct page {
	struct list list;
	atomic_t ref_count;  // Reference count for the page
	unsigned long flags; // Flags for the page (e.g., dirty, accessed)
};

static inline uintptr_t align_up_page(uintptr_t addr)
{
	return (addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}
static inline uintptr_t align_down_page(uintptr_t addr)
{
	return addr & ~(PAGE_SIZE - 1);
}

static inline uintptr_t pfn_to_phys(pfn_t pfn)
{
	return pfn << PAGE_SHIFT;
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
