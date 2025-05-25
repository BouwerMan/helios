/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stddef.h>
#include <stdlib.h>

#include <kernel/helios.h>
#include <kernel/memory/memory_limits.h>
#include <limine.h>

#define BITSET_WIDTH 64

#define END_LINK 0xDEADDEADDEADDEADULL

struct pmm {
	uintptr_t free_dma; // Head of ZONE_DMA linked stack
	size_t free_pages_dma;
	size_t total_pages_dma;
	uintptr_t free_dma32;
	size_t free_pages_dma32;
	size_t total_pages_dma32;
	uintptr_t free_norm;
	size_t free_pages_norm;
	size_t total_pages_norm;
};

// is this really needed? dumbass
static inline uint64_t phys_to_hhdm(uint64_t phys, uint64_t hhdm_offset)
{
	return phys + hhdm_offset;
}

static inline uint64_t get_phys_addr(uint64_t word_offset, uint64_t bit_offset)
{
	return (word_offset * 64 + bit_offset) * PAGE_SIZE;
}

static inline uint64_t get_word_offset(uint64_t phys_addr)
{
	return (phys_addr / PAGE_SIZE) / BITSET_WIDTH;
}

static inline uint64_t get_bit_offset(uint64_t phys_addr)
{
	return (phys_addr / PAGE_SIZE) % BITSET_WIDTH;
}

static inline uint64_t align_up(uint64_t addr)
{
	return (addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}
static inline uint64_t align_down(uint64_t addr)
{
	return addr & ~(PAGE_SIZE - 1);
}

void pmm_init(struct limine_memmap_response* mmap, uint64_t hhdm_offset);
void* pmm_alloc_page(void);
void pmm_free_page(void* addr);
void* pmm_alloc_contiguous(size_t count);
void pmm_free_contiguous(void* addr, size_t count);
bool pmm_page_is_used(uint64_t phys_addr);
size_t pmm_get_free_page_count(void);
size_t pmm_get_total_pages(void);

void pmm_test(void);
