/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <kernel/spinlock.h>
#include <mm/page.h>
#include <util/list.h>

#define MAX_ORDER 10 // 2^10 pages (1024 pages), or 4MiB blocks

struct free_area {
	struct list free_list; // Linked list of free blocks
};

struct buddy_allocator {
	struct list free_lists[MAX_ORDER + 1]; // One for each order
	size_t size;			       // Total size in bytes
	size_t min_order;
	size_t max_order;
	spinlock_t lock;
};

void page_alloc_init();

struct page* alloc_pages(unsigned long flags, size_t order);
struct page* alloc_page(unsigned long flags);

void free_page(void* addr);
void __free_page(struct page* page);
void __free_orphan_page(struct page* page);

void buddy_dump_free_lists();
