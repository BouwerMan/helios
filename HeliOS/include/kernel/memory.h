#pragma once

#include <kernel/interrupts.h>
#include <stdint.h>

#define GET_VIRT_ADDR(p_index, t_index) (((t_index * 1024) + p_index) * 4)
#define PAGE_FLAG_PRESENT (1 << 0)
#define PAGE_FLAG_WRITE (1 << 1)

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

void init_memory(uint32_t mem_high, uint32_t phys_alloc_start);
void pmm_init(uint32_t mem_low, uint32_t mem_high);
void invalidate(uint32_t vaddr);
void reload_cr3();
uintptr_t frame_alloc(unsigned int num_frames);
void page_fault(struct irq_regs* r);
