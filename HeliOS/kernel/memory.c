#include <kernel/interrupts.h>
#include <kernel/memory.h>
#include <kernel/multiboot.h>
#include <kernel/sys.h>
#include <kernel/tty.h>
#include <stdio.h>
#include <string.h>

static uint32_t page_frame_min;
static uint32_t page_frame_max;
static uint32_t total_alloc;
static uint32_t last_frame;

extern uint32_t kernel_end;

extern void write_cr3(uint32_t pointer);

#define PAGE_FLAG_PRESENT (1 << 0)
#define PAGE_FLAG_WRITE   (1 << 1)
// How many entries wide the frame bitset is
#define BITSET_WIDTH 32

#define INDEX_FROM_BIT(a)                 (a / BITSET_WIDTH)
#define OFFSET_FROM_BIT(a)                (a % BITSET_WIDTH)
#define GET_VIRT_ADDR(pd_index, tb_index) (((tb_index * 1024) + pd_index) * 4)

uint32_t* frame_bitset;
uint32_t nframes;

page_dir_t* kernel_page_dir;

uintptr_t placement_ptr;

void bootstrap_malloc_startat(uintptr_t address) { placement_ptr = address; }

// Only used for intial page table mapping.
// Phys does the conversion from virtual to phys for us
uintptr_t bootstrap_malloc_real(size_t size, int align, uintptr_t* phys)
{
    if (align && (placement_ptr & 0xFFFFF000)) {
        placement_ptr &= 0xFFFFF000;
    }
    if (phys) *phys = placement_ptr - KERNEL_OFFSET;
    uintptr_t addr = placement_ptr;
    placement_ptr += size;
    return addr;
}

// Initialize bitmap, only need stop location, will pull start from
// placement_ptr That way I can malloc the bitmap dynamically.
void pmm_init(uint32_t mem_high)
{
    uint32_t mem_low = placement_ptr - KERNEL_OFFSET;
    // round up division
    page_frame_min = CEIL_DIV(mem_low, PAGE_SIZE);
    page_frame_max = mem_high / PAGE_SIZE;
    nframes = (page_frame_max - page_frame_min) / BITSET_WIDTH;
    // nframes = page_frame_max - page_frame_min;
    total_alloc = 0;

    // Shrink number of frames
    nframes = nframes - (sizeof(uint32_t) * nframes) % PAGE_SIZE;
    // Allocate the bitmap
    frame_bitset
        = (uint32_t*)bootstrap_malloc_real(sizeof(uint32_t) * nframes, 1, NULL);
    // printf("MIN: 0x%X, MAX: 0x%X\n", page_frame_min, page_frame_max);
    // First set all to used
    memset(frame_bitset, 0xFF, sizeof(uint32_t) * nframes);
    // NOTE: for now i will just select placement_ptr as start for phys, unsure
    // if there is a better way. Now we set unused (past placement_ptr)
    // TODO: make this a memset?
    for (size_t i
         = CEIL_DIV((placement_ptr - KERNEL_OFFSET) / PAGE_SIZE, BITSET_WIDTH);
         i < INDEX_FROM_BIT(nframes); i++) {
        frame_bitset[i] = 0;
    }
}

// TODO: Separate kernel heap and userspace heap, right now i'm only working
// with kernel but
//       userspace should start at virt: 0x0
void init_memory(uint32_t mem_high, uint32_t phys_alloc_start)
{
    uint32_t address = (phys_alloc_start) & 0xFFFFF000;
#ifdef KERNEL_DEBUG
    printf("Initial PMM Address: 0x%X\n", address);
#endif
    bootstrap_malloc_startat(address + KERNEL_OFFSET);

    uintptr_t pd_phys;
    kernel_page_dir
        = (page_dir_t*)bootstrap_malloc_real(sizeof(page_dir_t), 1, &pd_phys);
    memset((unsigned char*)kernel_page_dir, 0, sizeof(page_dir_t));
    kernel_page_dir->physical_addr = pd_phys;
    uintptr_t map_addr = 0x0;
    // Allocate space for paging structure. Only up until 1022, since 1023 will
    // point to beginning of directory. Also map kernel space.
    for (size_t i = 0; i < 1023; i++) {
        uintptr_t table_physaddr;
        kernel_page_dir->tables[i] = (page_table_t*)bootstrap_malloc_real(
            sizeof(page_table_t), 1, &table_physaddr);
        kernel_page_dir->physical_tables[i] = table_physaddr | 0x2;
        // Map kernel space (above 0xC0000000)
        if (i >= 768) {
            for (size_t j = 0; j < 1024; j++) {
                kernel_page_dir->tables[i]->pages[j] = map_addr | 0x3;
                map_addr += PAGE_SIZE;
            }
            kernel_page_dir->physical_tables[i] |= 0x3;
        }
    }
    // Map last entry to beginning of directory
    kernel_page_dir->physical_tables[1023]
        = kernel_page_dir->physical_addr | 0x3;

    write_cr3((uint32_t)kernel_page_dir->physical_addr);

    pmm_init(mem_high);
    reload_cr3();
    install_isr_handler(14, page_fault);
}

// This might just invalidate the TLB telling the processor to double check the
// page dir
void invalidate(uint32_t vaddr) { asm volatile("invlpg %0" ::"m"(vaddr)); }

void reload_cr3()
{
    asm volatile("movl %%cr3, %%eax\n"
                 "movl %%eax, %%cr3"
                 :
                 :
                 : "%eax");
}

uintptr_t get_physaddr(uintptr_t virtualaddr)
{
    unsigned long pdindex = (unsigned long)virtualaddr >> 22;
    unsigned long ptindex = (unsigned long)virtualaddr >> 12 & 0x03FF;

    unsigned long* pd = (unsigned long*)0xFFFFF000;
    // TODO: Here you need to check whether the PD entry is present.

    unsigned long* pt = ((unsigned long*)0xFFC00000) + (0x400 * pdindex);
    // TODO: Here you need to check whether the PT entry is present.

    return (uintptr_t)((pt[ptindex] & ~0xFFF)
                       + ((unsigned long)virtualaddr & 0xFFF));
}

// TODO: Maxes out at 32 frames.
// TODO: Fragmentation issues when num_frames goes past 32-bit border (between
// indexes)
//
// Gets frame number from memory bitset
uint32_t find_frames(size_t num_frames)
{
    size_t init_offset = 0;
    size_t consecutive_free = 0;
    // iterate through bitset, then if we have enough frames we break out to
    // mark them
    for (size_t i = 0; i < INDEX_FROM_BIT(nframes); i++) {
        // Means no bits are free
        if (frame_bitset[i] == 0xFFFFFFFF) {
            init_offset = 0;
            consecutive_free = 0;
            continue;
        }
        // Iterate through all bits
        for (int j = 0; j < BITSET_WIDTH; j++) {
            uint32_t to_test = 1 << j;
            if (!(frame_bitset[i] & to_test)) {
                if (!init_offset) init_offset = j;
                consecutive_free++;
            } else if (consecutive_free > 0 && consecutive_free < num_frames) {
                init_offset = 0;
                consecutive_free = 0;
            }
        }
        if (consecutive_free >= num_frames) {
            return ((i * BITSET_WIDTH) + init_offset);
        } else {
            consecutive_free = 0;
            init_offset = 0;
        }
    }

    // If no memory is found
    return 0;
}

/// I beliebe frame_addr is virtual addr
static void set_frame(uintptr_t frame_addr)
{
    uint32_t frame = frame_addr / PAGE_SIZE;
    uint32_t idx = INDEX_FROM_BIT(frame);
    uint32_t off = OFFSET_FROM_BIT(frame);
    frame_bitset[idx] |= (0x1 << off);
}

/// I beliebe frame_addr is virtual addr
static void clear_frame(uintptr_t frame_addr)
{
    uint32_t frame = frame_addr / PAGE_SIZE;
    uint32_t idx = INDEX_FROM_BIT(frame);
    uint32_t off = OFFSET_FROM_BIT(frame);
    frame_bitset[idx] &= ~(0x1 << off);
}

uintptr_t kalloc_frames(size_t num_frames)
{
    if (num_frames == 0) return NULL;
    uint32_t first_frame = find_frames(num_frames);
    if (first_frame == 0) return NULL;
    for (size_t i = first_frame; i < num_frames + first_frame; i++) {
        // printf("allocating frame 0x%X\n", (i * PAGE_SIZE));
        set_frame(i * PAGE_SIZE);
    }
    // printf("\n");
    return first_frame * PAGE_SIZE + KERNEL_OFFSET;
}

// TODO: return error code if freeing failed
void kfree_frames(uintptr_t first_frame, size_t num_frames)
{
    if (num_frames == 0) return;
    for (size_t i = 0; i < num_frames; i++) {
        // printf("Freeing frame 0x%X\n", first_frame + (i * PAGE_SIZE) -
        // KERNEL_OFFSET);
        clear_frame(first_frame + (i * PAGE_SIZE) - KERNEL_OFFSET);
    }
}

void page_fault(struct irq_regs* r)
{
    uint32_t fault_addr;
    asm volatile("mov %%cr2, %0" : "=r"(fault_addr));

    int present = !(r->err_code & 0x1);
    int rw = r->err_code & 0x2;
    int user = r->err_code & 0x4;
    int reserved = r->err_code & 0x8;
    int id = r->err_code & 0x10;

    printf("PAGE FAULT! p:%d,rw:%d,user:%d,res:%d,id:%d) at 0x%x\n", present,
           rw, user, reserved, id, fault_addr);
    printf("Caused by 0x%X\n", r->eip);
    panic("Page Fault");
}

// TODO: Should check for large fragmentation
// TODO: Should we return error code instead of panicing and printing a ton of
// stuff?
uint8_t test_pmm()
{
    bool passed = true;
    uint8_t err_code = UNKNOWN;
    puts("PMM Testing:");
    // Get initial free location to compare after everything is freed
    uintptr_t init_frame = kalloc_frames(1);
    // printf("Initial 1 frame alloc: 0x%X\n", init_frame);
    kfree_frames(init_frame, 1);
    uintptr_t allocated[64] = { 0 };
    // allocate a bunch of frames
    for (size_t i = 0; i < 32; i++) {
        allocated[i] = kalloc_frames(i + 1);
        if (i > 0) {
            if (allocated[i] - allocated[i - 1] < (i - 1) * PAGE_SIZE) {
                passed = false;
                err_code |= OVERLAP;
                err_code &= ~UNKNOWN;
                break;
            }
        }
    }
    // free those frames
    for (size_t i = 0; i < 32; i++) {
        kfree_frames(allocated[i], i + 1);
    }
    // Allocate them again except save them at i + 32 in our allocated array
    for (size_t i = 0; i < 32; i++) {
        allocated[i + 32] = kalloc_frames(i + 1);
        if (i > 0) {
            if (allocated[i] - allocated[i - 1] < (i - 1) * PAGE_SIZE) {
                passed = false;
                err_code |= OVERLAP;
                err_code &= ~UNKNOWN;
                break;
            }
        }
    }
    for (size_t i = 0; i < 32; i++) {
        kfree_frames(allocated[i + 32], i + 1);
    }

    // These addresses should be the same since we freed everything
    size_t i = 0, j = 32;
    for (; i < 32 && j < 64; i++, j++) {
        if (allocated[i] != allocated[j]) {
            passed = false;
            err_code |= RUN_DIFF;
            err_code &= ~UNKNOWN;
            break;
        }
    }

    // Making sure freeing works properly, this should match init_frame
    uintptr_t after_frame = kalloc_frames(1);
    // printf("After 1 frame alloc: 0x%X\n", after_frame);
    kfree_frames(after_frame, 1);
    if (init_frame != after_frame) {
        passed = false;
        err_code |= IA_DIFF;
        err_code &= ~UNKNOWN;
    }

    if (passed) err_code = PASSED;

    return err_code;
}
