#pragma once
#include <limine.h>
#include <stddef.h>
#include <stdint.h>

// #define PHYS_TO_HHDM(x)   ((void *)((uintptr_t)(x) + hhdm_offset))
// #define HHDM_TO_PHYS(x)   ((uintptr_t)(x) - hhdm_offset)
//
// #define PHYS_TO_KERNEL(x) ((void *)((uintptr_t)(x) + KERNEL_VIRT_OFFSET))  // for higher-half kernel
// #define KERNEL_TO_PHYS(x) ((uintptr_t)(x) - KERNEL_VIRT_OFFSET)

#define LOW_IDENTITY	   0x4000000 // 64 MiB
#define PAGE_TABLE_ENTRIES 512
#define KERNEL_HEAP_BASE   0xFFFFFFFFC0000000UL
#define KERNEL_HEAP_LIMIT  0xFFFFFFFFE0000000UL
#define KERNEL_VIRT_BASE   0xFFFFFFFF80000000UL
#define HHDM_OFFSET	   0xffff800000000000UL // TODO: make this not hardcoded
#define PHYS_TO_VIRT(p)	   ((void*)((uintptr_t)(p) + HHDM_OFFSET))
#define VIRT_TO_PHYS(v)	   ((uintptr_t)(v) - HHDM_OFFSET)

#define FLAGS_MASK	   0xFFF
#define PAGE_FRAME_MASK	   (~0xFFFULL)
#define PAGE_PRESENT	   (1ULL << 0) // Page is present in memory
#define PAGE_WRITE	   (1ULL << 1) // Writable
#define PAGE_USER	   (1ULL << 2) // Accessible from user-mode
#define PAGE_WRITE_THROUGH (1ULL << 3) // Write-through caching enabled
#define PAGE_CACHE_DISABLE (1ULL << 4) // Disable caching
#define PAGE_ACCESSED	   (1ULL << 5) // Set by CPU when page is read/written
#define PAGE_DIRTY	   (1ULL << 6) // Set by CPU on write
#define PAGE_HUGE	   (1ULL << 7) // 2 MiB or 1 GiB page (set only in PD or PDPT)
#define PAGE_GLOBAL	   (1ULL << 8)	// Global page (ignores CR3 reload)
#define PAGE_NO_EXECUTE	   (1ULL << 63) // Requires EFER.NXE to be set

void vmm_init(struct limine_memmap_response* mmap,
	      struct limine_executable_address_response* exe,
	      uint64_t hhdm_offset);
void vmm_map(void* virt_addr, void* phys_addr, uint64_t flags);
void vmm_unmap(void* virt_addr, bool free_phys);
void* vmm_alloc_pages(size_t pages);
void vmm_free_pages(void* addr, size_t count);
// For testing
void* vmm_translate(void* virt_addr);
void vmm_dump_page_table();
