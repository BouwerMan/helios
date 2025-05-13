#pragma once
#include <kernel/helios.h>
#include <limine.h>
#include <stddef.h>

#define PAGE_SIZE 0x1000
_Static_assert(IS_POWER_OF_TWO(PAGE_SIZE) == true, "PAGE_SIZE must be power of 2");
#define BITSET_WIDTH 64

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
