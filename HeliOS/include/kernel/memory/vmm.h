#pragma once
#include <stdint.h>

#define LOW_IDENTITY	   0x4000000 // 64 MiB
#define PAGE_TABLE_ENTRIES 512
#define KERNEL_VIRT_BASE   0xFFFFFFFF80000000
#define PHYS_TO_VIRT(p)	   ((void*)((uintptr_t)(p) + KERNEL_VIRT_BASE))
#define VIRT_TO_PHYS(v)	   ((uintptr_t)(v) - KERNEL_VIRT_BASE)

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

void vmm_init();
void vmm_map(void* virt_addr, void* phys_addr, uint64_t flags);
void vmm_unmap(void* virt_addr, bool free_phys);
// For testing
void* vmm_translate(void* virt_addr);
