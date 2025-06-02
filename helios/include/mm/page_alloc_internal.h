/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stddef.h>

#include <mm/page.h>

extern struct buddy_allocator alr;

struct page* __alloc_pages_core(struct buddy_allocator* allocator, flags_t flags, size_t order);
void __free_pages_core(struct buddy_allocator* allocator, struct page* page, size_t order);
