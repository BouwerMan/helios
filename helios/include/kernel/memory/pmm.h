/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stddef.h>

#include <kernel/helios.h>
#include <limine.h>
#include <stdlib.h>

#define PAGE_SIZE 0x1000ULL
_Static_assert(IS_POWER_OF_TWO(PAGE_SIZE) == true, "PAGE_SIZE must be power of 2");

#define BITSET_WIDTH 64

#define ZONE_DMA_BASE  0x0
#define ZONE_DMA_LIMIT 0xffffffULL

#define ZONE_DMA32_BASE	 0x1000000ULL
#define ZONE_DMA32_LIMIT 0xffffffffULL

#define ZONE_NORMAL_BASE 0x100000000ULL

#define END_LINK 0xDEADDEADDEADDEADULL

enum MEMORY_ZONES {
	ZONE_DMA,    // Under 16 MiB (unimplemented lmao)
	ZONE_DMA32,  // Under 4 GiB
	ZONE_NORMAL, // Over 4 GiB
};

struct free_block {
	uintptr_t start;
	size_t len;
	struct list link;
};

struct pmm {
	uintptr_t free_dma; // Head of ZONE_DMA linked stack
	struct list f_dma;
	uintptr_t free_dma_end;
	uintptr_t free_dma32;
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
