#include "../arch/i386/vga.h"
#include <kernel/cpu.h>
#include <kernel/gdt.h>
#include <kernel/interrupts.h>
#include <kernel/keyboard.h>
#include <kernel/mm.h>
#include <kernel/multiboot.h>
#include <kernel/paging.h>
#include <kernel/sys.h>
#include <kernel/timer.h>
#include <kernel/tty.h>
#include <stdio.h>

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
    /* Loop through the memory map and display the values */
    int i;
    uint32_t length = 0;
    for (i = 0; i < mbd->mmap_length;
         i += sizeof(multiboot_memory_map_t)) {
        multiboot_memory_map_t* mmmt = (multiboot_memory_map_t*)(mbd->mmap_addr + i);

        printf("Start Addr: %x | Length: %x | Size: %x | Type: %d\n",
            mmmt->addr_low, mmmt->len_low, mmmt->size, mmmt->type);

        length += mmmt->len_low;
        if (mmmt->type == MULTIBOOT_MEMORY_AVAILABLE) {
            /*
             * Do something with this memory block!
             * BE WARNED that some of memory shown as availiable is actually
             * actively being used by the kernel! You'll need to take that
             * into account before writing to memory!
             */
        }
    }
    printf("Memory Length: %dMB\n", length / 1024 / 1024);

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

    puts("Initializing Timer");
    timer_init();

    puts("Initializing Keyboard");
    keyboard_init();

    puts("Initalizing Paging");
    mmap_init(mbd);

    printf("Memory: %d\n", mbd->mem_upper - mbd->mem_lower);
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

    tty_writestring("Printf testing\n");
    putchar('c');
    printf("test old\n");

#define PRINTF_TESTING
#ifdef PRINTF_TESTING
    printf("test new\n");
    printf("String: %s\n", "test string");
    printf("Char: %c\n", 't');
    printf("Hex: 0x%x 0x%X\n", 0x14AF, 0x41BC);
    printf("pos dec: %d\n", 5611);
    printf("neg dec: %d\n", -468);
    printf("unsigned int: %d\n", 4184);
    printf("oct: %o\n", 4184);
#endif // PRINTF_TESTING
#if 0
    page_directory_clear();
    init_paging();
#endif

#if 0
    // Testing paging
    uint32_t new_frame = allocate_frame();
    uint32_t new_frame_addr = mmap_read(new_frame, MMAP_GET_ADDR);
    printf("New frame allocated at: 0x%x\n", new_frame_addr);
    uint32_t new_frame_two = allocate_frame();
    uint32_t new_frame_addr_two = mmap_read(new_frame_two, MMAP_GET_ADDR);
    printf("New frame allocated at: 0x%x\n", new_frame_addr_two);
    free_frame(new_frame);
    uint32_t new_frame_three = allocate_frame();
    uint32_t new_frame_addr_three = mmap_read(new_frame, MMAP_GET_ADDR);
    printf("New frame allocated at: 0x%x\n", new_frame_addr);
#endif

    // Stopping us from exiting kernel
    for (;;)
        ;
}
