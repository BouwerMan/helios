#pragma once
// Contains Physical Memory Manager

#include <kernel/interrupts.h>
#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE 4096

#if 0
typedef struct page {
    uint32_t present : 1;
    uint32_t rw : 1;
    uint32_t user : 1;
    uint32_t accessed : 1;
    uint32_t dirty : 1;
    uint32_t unused : 7;
    uint32_t frame : 20;
} page_t;
#endif

// TODO: use bit field for pages?
typedef struct page_table {
    uintptr_t pages[1024] __attribute__((aligned(4096)));
    // page_t pages[1024];
} page_table_t;

typedef struct page_dir {
    // Physical location of each table, this is what the processor looks at
    uintptr_t physical_tables[1024] __attribute__((aligned(4096)));
    page_table_t* tables[1024]; // How i represent page tables, processor should peak at this
    uintptr_t physical_addr; // Physical address where this is stored
} page_dir_t;

enum PMM_ERROR_CODE {
    PASSED = 0, // All tests passed
    UNKNOWN = (1 << 0),
    OVERLAP = (1 << 1), // Allocating multiple frames leads to overlaping memory regions
    IA_DIFF = (1 << 2), // Allocating before and after test resulted in different frames
    RUN_DIFF = (1 << 3), // Allocating differed between both runs
};

void init_memory(uint32_t mem_high, uint32_t phys_alloc_start);
void pmm_init(uint32_t mem_high);
void invalidate(uint32_t vaddr);
void reload_cr3();
uintptr_t get_physaddr(uintptr_t virtualaddr);
uint32_t find_frames(size_t num_frames);
uintptr_t kalloc_frames(size_t num_frames);
/// Frees contiguous region of frames
void kfree_frames(uintptr_t first_frame, size_t num_frames);
void page_fault(struct irq_regs* r);
/// Tests pmm functionality. Fails if not successful.
uint8_t test_pmm();
