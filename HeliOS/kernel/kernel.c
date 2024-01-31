#include "../arch/i386/vga.h"
#include <kernel/cpu.h>
#include <kernel/gdt.h>
#include <kernel/interrupts.h>
#include <kernel/keyboard.h>
#include <kernel/mm.h>
#include <kernel/multiboot.h>
#include <kernel/sys.h>
#include <kernel/timer.h>
#include <kernel/tty.h>
#include <stdio.h>

void kernel_main(multiboot_info_t* mbd, uint32_t magic)
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

        printf("CPU: ");
        cpu_print_model();

        tty_enable_cursor(0, 0);
        // tty_disable_cursor();
        printf("Welcome to %s. Version: %s\n", KERNEL_NAME, KERNEL_VERSION);

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

        // Testing that interrupts are active by waiting for the timer to tick
        puts("Testing Interrupts");
        timer_poll();
        tty_setcolor(VGA_COLOR_GREEN);
        puts("Interrupts passed");

        tty_setcolor(VGA_COLOR_LIGHT_GREY);
        puts("Initializing Keyboard");
        keyboard_init();

        puts("Initalizing Paging");
        mmap_init(mbd);
        uint32_t new_frame = allocate_frame();
        uint32_t new_frame_addr = mmap_read(new_frame, MMAP_GET_ADDR);
        printf("New frame allocated at: 0x%x\n", new_frame_addr);
        uint32_t new_frame_two = allocate_frame();
        uint32_t new_frame_addr_two = mmap_read(new_frame_two, MMAP_GET_ADDR);
        printf("New frame allocated at: 0x%x\n", new_frame_addr_two);

        // Stopping us from exiting kernel
        for (;;)
                ;
}
