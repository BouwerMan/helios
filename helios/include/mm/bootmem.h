/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stddef.h>

#include <limine.h>

extern void bootmem_init(struct limine_memmap_response* mmap);
extern void* bootmem_alloc_page(void);
extern void bootmem_free_page(void* addr);
extern void* bootmem_alloc_contiguous(size_t count);
extern void bootmem_free_contiguous(void* addr, size_t count);
extern bool bootmem_page_is_used(uintptr_t phys_addr);
