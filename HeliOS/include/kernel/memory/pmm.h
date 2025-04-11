#pragma once
#include <limine.h>
#include <stddef.h>

#define IS_POWER_OF_TWO(x) (((x) != 0) && (((x) & ((x) - 1)) == 0))
#define PAGE_SIZE          0x1000
_Static_assert(IS_POWER_OF_TWO(PAGE_SIZE) == true, "PAGE_SIZE must be power of 2");

// is this really needed? dumbass
static inline uint64_t phys_to_hhdm(uint64_t phys, uint64_t hhdm_offset) { return phys + hhdm_offset; }

static inline uint64_t align_up(uint64_t addr) { return (addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1); }
static inline uint64_t align_down(uint64_t addr) { return addr & ~(PAGE_SIZE - 1); }

void pmm_init(struct limine_memmap_response* mmap, uint64_t hhdm_offset);
void* pmm_alloc_page(void);
void pmm_free_page(void* addr);
void* pmm_alloc_contiguous(size_t count);
bool pmm_page_is_used(uint64_t phys_addr);
size_t pmm_get_free_page_count(void);
size_t pmm_get_total_pages(void);
