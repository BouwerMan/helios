#include <kernel/interrupts.h>
#include <kernel/memory.h>
#include <kernel/multiboot.h>
#include <kernel/sys.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t page_frame_min;
static uint32_t page_frame_max;
static uint32_t total_alloc;
static uint32_t last_frame;

extern uint32_t kernel_end;

extern void write_cr3(uint32_t pointer);

#define PAGE_SIZE 4096
#define NUM_PAGE_DIRS 256
// page_dir_t page_dirs[NUM_PAGE_DIRS];
// page_dir_t* kernel_dir;
// page_dir_t* current_dir;

#define NUM_PAGE_FRAMES (0x100000000 / 0x1000 / 8)
// FIX: Hardcoded mem_high for qemu only
// #define NUM_PAGE_FRAMES (0xBFEE0000 / PAGE_SIZE)

// TODO: Dynamically, bit array
uint8_t phys_memory_bitmap[NUM_PAGE_FRAMES / 8];

// Might be an issue here, idk how far we allocated idk if i can accidentally overwrite smthn
uintptr_t pg_phys;
uint32_t* page_directory __attribute__((aligned(4096)));
uint32_t* page_table __attribute__((aligned(4096)));

page_dir_t* page_dir;
page_table_t* page_tab;

uintptr_t placement_ptr;

// TODO: Add to header file
void malloc_startat(uintptr_t address) { placement_ptr = address; }

// Phys does the conversion from virtual to phys for us
uintptr_t malloc_real(size_t size, int align, uintptr_t* phys)
{
    if (align && (placement_ptr & 0xFFFFF000)) {
        placement_ptr &= 0xFFFFF000;
    }
    // TODO: This only works for kernel malloc
    if (phys) *phys = placement_ptr - KERNEL_OFFSET;
    uintptr_t addr = placement_ptr;
    placement_ptr += size;
    return addr;
}

void pmm_init(uint32_t mem_low, uint32_t mem_high)
{
    // round up division
    page_frame_min = CEIL_DIV(mem_low, 0x1000);
    page_frame_max = mem_high / 0x1000;
    total_alloc = 0;
    printf("MIN: 0x%X, MAX: 0x%X\n", page_frame_min, page_frame_max);

    memset(phys_memory_bitmap, 0, sizeof(phys_memory_bitmap));
}

// TODO: Should be able to forget abt the init_page_dir as long as i identity map the kernel
void init_memory(uint32_t mem_high, uint32_t phys_alloc_start)
{
    irq_install_handler(14, page_fault);

    uint32_t address = (phys_alloc_start) & 0xFFFFF000;
    printf("Initial Phys Address: 0x%X\n", address);
    malloc_startat(address + KERNEL_OFFSET);

    page_directory = (uint32_t*)malloc_real(sizeof(uint32_t*) * 1024, 1, &pg_phys);
    uintptr_t pd_phys;
    page_dir = (page_dir_t*)malloc_real(sizeof(page_dir_t), 1, &pd_phys);
    memset((unsigned char*)page_dir, 0, sizeof(page_dir_t));
    page_dir->physical_addr = pd_phys;
    printf("Page dir: 0x%X, new placement_ptr: 0x%X\n", page_dir->physical_addr, placement_ptr);
    // Set each entry to not present
    for (size_t i = 0; i < 1024; i++) {
        uintptr_t phys;
        page_dir->tables[i] = (page_table_t*)malloc_real(sizeof(page_table_t), 1, &phys);
        page_dir->physical_tables[i] = phys | 0x2;
        page_directory[i] = 0x2;
    }

    // create and fill page table
    uintptr_t tb_phys;
    page_table = (uint32_t*)malloc_real(sizeof(uint32_t*) * 1024, 1, &tb_phys);
    address += 0xF0000;
    for (size_t i = 0; i < 1024; i++) {
        page_table[i] = address | 3;
        page_dir->tables[340]->pages[i] = address | 3;
        address += 0x1000;
        // page_dir->tables[340]->pages[i].frame = address;
        // page_dir->tables[340]->pages[i].present = 1;
        // page_dir->tables[340]->pages[i].rw = 1;
    }
    page_dir->physical_tables[340] |= 3;

    page_directory[340] = ((uint32_t)tb_phys) | 3;
    // FIX: TMP
    page_directory[768] = 0x81;
    page_dir->physical_tables[768] = 0x81;
    // page_directory[768] = (0 << 22) | 0x81;
    // page_directory[769] = (1 << 22) | 0x81;
    // page_directory[770] = (2 << 22) | 0x81;
    // page_directory[771] = (3 << 22) | 0x81;
    printf("340: 0x%X, location: 0x%X, 15: 0x%X\n", page_directory[340], (uint32_t)pg_phys,
        page_directory[15]);
    // asm volatile("xchgw %bx, %bx");
    write_cr3((uint32_t)page_dir->physical_addr);

    pmm_init(phys_alloc_start, mem_high);
    reload_cr3();
}

// This might just invalidate the TLB telling the processor to double check the page dir
void invalidate(uint32_t vaddr) { asm volatile("invlpg %0" ::"m"(vaddr)); }

void reload_cr3()
{
    asm volatile("movl %%cr3, %%eax\n"
                 "movl %%eax, %%cr3"
                 :
                 :
                 : "%eax");
}

uintptr_t frame_alloc(unsigned int num_frames)
{
    puts("Finding frame");
    size_t consecutive_free = 0;
    // iterate through bitmap, then if we have enough frames we break out to mark them
    for (size_t i = 0; i < NUM_PAGE_FRAMES; i++) {
        if (!(phys_memory_bitmap[i / 8] & (1 << (i % 8)))) {
            consecutive_free++;
            if (consecutive_free == num_frames) {
                // allocate these all
                uintptr_t start_addr = (i - num_frames + 1) * PAGE_SIZE;
                for (size_t j = i - num_frames + 1; j <= i; j++) {
                    // mark each page as allocated in the bitmap
                    phys_memory_bitmap[j / 8] |= (1 << (j % 8));
                }
                return start_addr;
            }
        } else {
            consecutive_free = 0;
        }
    }

    puts("Could not find frame :(");
    // If no memory is found
    return 0;
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

    printf("PAGE FAULT! p:%d,rw:%d,user:%d,res:%d,id:%d) at 0x%x\r\n", present, rw, user, reserved,
        fault_addr, id);
    panic("Page Fault");
}
