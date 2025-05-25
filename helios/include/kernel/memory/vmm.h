/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stddef.h>

#include <kernel/memory/memory_limits.h>
#include <limine.h>

// #define PHYS_TO_HHDM(x)   ((void *)((uintptr_t)(x) + hhdm_offset))
// #define HHDM_TO_PHYS(x)   ((uintptr_t)(x) - hhdm_offset)
//
// #define PHYS_TO_KERNEL(x) ((void *)((uintptr_t)(x) + KERNEL_VIRT_OFFSET))  // for higher-half kernel
// #define KERNEL_TO_PHYS(x) ((uintptr_t)(x) - KERNEL_VIRT_OFFSET)

#define LOW_IDENTITY	   0x4000000 // 64 MiB
#define PAGE_TABLE_ENTRIES 512
#define KERNEL_HEAP_BASE   0xFFFFFFFFC0000000ULL
#define KERNEL_HEAP_LIMIT  0xFFFFFFFFE0000000ULL
#define KERNEL_POOL	   (KERNEL_HEAP_LIMIT - KERNEL_HEAP_BASE)
// #define PHYS_TO_VIRT(p)	   ((void*)((uintptr_t)(p) + HHDM_OFFSET))
// #define VIRT_TO_PHYS(v)	   ((uintptr_t)(v) - HHDM_OFFSET)

#define FLAGS_MASK	   0xFFFULL
#define PAGE_FRAME_MASK	   (~0xFFFULL)
#define PAGE_PRESENT	   (1ULL << 0)	// Page is present in memory
#define PAGE_WRITE	   (1ULL << 1)	// Writable
#define PAGE_USER	   (1ULL << 2)	// Accessible from user-mode
#define PAGE_WRITE_THROUGH (1ULL << 3)	// Write-through caching enabled
#define PAGE_CACHE_DISABLE (1ULL << 4)	// Disable caching
#define PAGE_ACCESSED	   (1ULL << 5)	// Set by CPU when page is read/written
#define PAGE_DIRTY	   (1ULL << 6)	// Set by CPU on write
#define PAGE_HUGE	   (1ULL << 7)	// 2 MiB or 1 GiB page (set only in PD or PDPT)
#define PAGE_GLOBAL	   (1ULL << 8)	// Global page (ignores CR3 reload)
#define PAGE_NO_EXECUTE	   (1ULL << 63) // Requires EFER.NXE to be set

// TODO: Dynamic MAX_ORDER
#define MAX_ORDER   29
#define MIN_ORDER   12 // 4 KiB blocks (2^12)
#define BUDDY_NODES ((1 << (MAX_ORDER - MIN_ORDER + 1)) - 1)

enum BLOCK_STATE {
	BLOCK_INVALID,
	BLOCK_FREE,
	BLOCK_SPLIT,
	BLOCK_ALLOCATED,
};

struct buddy_allocator {
	struct list free_lists[MAX_ORDER + 1]; // One for each order
	uintptr_t base;			       // Base virtual address this allocator manages
	size_t size;			       // Total size in bytes
};

struct buddy_block {
	struct list link;
	uint8_t order;
	uint8_t state;
};

// TODO: Move to kmath
static inline size_t round_to_power_of_2(size_t n)
{
	n--;
	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
	n |= n >> 16;
	n |= n >> 32;
	return ++n;
}

/**
 * @brief Reads the value of the CR3 register.
 *
 * The CR3 register contains the physical address of the page directory base
 * register (PDBR) in x86 architecture. This function uses inline assembly
 * to retrieve the value of CR3 and return it.
 *
 * @return The value of the CR3 register as a uintptr_t.
 */
static inline uintptr_t vmm_read_cr3(void)
{
	uintptr_t cr3;
	__asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
	return cr3;
}

void vmm_init(struct limine_memmap_response* mmap, struct limine_executable_address_response* exe,
	      uint64_t hhdm_offset);
void vmm_map(void* virt_addr, void* phys_addr, uint64_t flags);
void vmm_unmap(void* virt_addr, bool free_phys);
void* vmm_alloc_pages(size_t count, bool contiguous);
void vmm_free_pages(void* addr, size_t count);
// For testing
void* vmm_translate(void* virt_addr);
void* recursive_vmm_translate(void* addr);
uint64_t test_virt_to_phys(uint64_t virt_addr);
void vmm_dump_page_table();

// Buddy allocator stuff

void* valloc(size_t pages, size_t flags);
void vfree(void* addr);
void vfree_pages(void* addr, size_t size);
