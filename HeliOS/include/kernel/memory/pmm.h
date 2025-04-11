#pragma once
#include <limine.h>
#include <stddef.h>

void pmm_init(struct limine_memmap_response* mmap);
void* pmm_alloc_page(void);
void pmm_free_page(void* addr);
void* pmm_alloc_contiguous(size_t count);
