#include "../arch/i386/vga.h"
#include <kernel/cpu.h>
#include <kernel/gdt.h>
#include <kernel/interrupts.h>
#include <kernel/keyboard.h>
#include <kernel/memory.h>
#include <kernel/multiboot.h>
#include <kernel/sys.h>
#include <kernel/timer.h>
#include <kernel/tty.h>
#include <stdio.h>

extern uint32_t kernel_start_raw;
extern uint32_t kernel_end_raw;
static uint32_t kernel_start;
static uint32_t kernel_end;

void kernel_early(multiboot_info_t* mbd, uint32_t magic)
{
    /* Initialize terminal interface */
    tty_initialize();

    /* Make sure the magic number matches for memory mapping*/
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        panic("invalid magic number!");
    }

    /* Check bit 6 to see if we have a valid memory map */
    if (!(mbd->flags >> 6 & 0x1)) {
        panic("invalid memory map given by GRUB bootloader");
    }
    // /* Loop through the memory map and display the values */
    // int i;
    // uint32_t length = 0;
    // for (i = 0; i < mbd->mmap_length; i += sizeof(multiboot_memory_map_t)) {
    //     multiboot_memory_map_t* mmmt = (multiboot_memory_map_t*)(mbd->mmap_addr + i);
    //
    //     printf("Start Addr: %x | Length: %x | Size: %x | Type: %d\n", mmmt->addr_low,
    //     mmmt->len_low,
    //         mmmt->size, mmmt->type);
    //
    //     length += mmmt->len_low;
    //     if (mmmt->type == MULTIBOOT_MEMORY_AVAILABLE) {
    //         /*
    //          * Do something with this memory block!
    //          * BE WARNED that some of memory shown as availiable is actually
    //          * actively being used by the kernel! You'll need to take that
    //          * into account before writing to memory!
    //          */
    //     }
    // }
    // printf("Memory Length: %dMB\n", length / 1024 / 1024);

    tty_enable_cursor(0, 0);
    // tty_disable_cursor();

    puts("Initializing GDT");
    gdt_init();

    puts("Initializing IDT");
    idt_init();

    puts("Initializing ISRs");
    isr_init();

    puts("Initializing IRQs");
    irq_init();

    // Have to do some maintenance to make these sane numbers
    kernel_start = (uint32_t)(&kernel_start_raw);
    kernel_end = (uint32_t)(&kernel_end_raw) - KERNEL_OFFSET;

    uint32_t phys_alloc_start = (kernel_end + 0x1000) & 0xFFFFF000;
    printf("KERNEL START: 0x%X, KERNEL END: 0x%X\n", kernel_start, kernel_end);
    printf("MEM LOW: 0x%X, MEM HIGH: 0x%X, PHYS START: 0x%X\n", mbd->mem_lower * 1024,
        mbd->mem_upper * 1024, phys_alloc_start);
    init_memory(mbd->mem_upper * 1024, phys_alloc_start);

    puts("Initializing Timer");
    timer_init();

    puts("Initializing Keyboard");
    keyboard_init();
}

void kernel_main()
{
    printf("Welcome to %s. Version: %s\n", KERNEL_NAME, KERNEL_VERSION);
    printf("Detected CPU: ");
    cpu_print_model();

    // Testing that interrupts are active by waiting for the timer to tick
    puts("Testing Interrupts");
    timer_poll();
    tty_setcolor(VGA_COLOR_GREEN);
    puts("Interrupts passed");
    tty_setcolor(VGA_COLOR_LIGHT_GREY);

    printf("Memory testing:\n");
    // FIX: Page faulting
    uintptr_t test1 = frame_alloc(1);
    printf("1 frame: 0x%X\n", test1);
    // int* test2 = (int*)test1;
    // *test2 = 251;
    // printf("test2: %d", *test2);
    // printf("irq handler: 0x%X\n", irq_routines[14]);
    // printf("Test 0x15CC314:\n");
    // uintptr_t test2 = 0x15CC314;
    // *(int*)test2 = 69;

#ifdef PRINTF_TESTING
    tty_writestring("Printf testing:\n");
    putchar('c');
    printf("test old\n");
    printf("test new\n");
    printf("String: %s\n", "test string");
    printf("Char: %c\n", 't');
    printf("Hex: 0x%x 0x%X\n", 0x14AF, 0x41BC);
    printf("pos dec: %d\n", 5611);
    printf("neg dec: %d\n", -468);
    printf("unsigned int: %d\n", 4184);
    printf("oct: %o\n", 4184);
#endif // PRINTF_TESTING

    // NOTE: I removed this for loop since that should reduce idle cpu usage (boot.asm calls hlt)
    //
    // Stopping us from exiting kernel
    // for (;;)
    //     ;
}
