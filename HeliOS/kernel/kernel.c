#include <kernel/gdt.h>
#include <kernel/interrupts.h>
#include <kernel/keyboard.h>
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

        /* Loop through the memory map and display the values */
        int i;
        for (i = 0; i < mbd->mmap_length;
             i += sizeof(multiboot_memory_map_t)) {
                multiboot_memory_map_t* mmmt = (multiboot_memory_map_t*)(mbd->mmap_addr + i);

                printf("Start Addr: %x | Length: %x | Size: %x | Type: %d\n",
                    mmmt->addr_low, mmmt->len_low, mmmt->size, mmmt->type);

                if (mmmt->type == MULTIBOOT_MEMORY_AVAILABLE) {
                        /*
                         * Do something with this memory block!
                         * BE WARNED that some of memory shown as availiable is actually
                         * actively being used by the kernel! You'll need to take that
                         * into account before writing to memory!
                         */
                }
        }

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
        puts("Interrupts passed");

        puts("Initializing Keyboard");
        keyboard_init();

        printf("Hex test: 0x%x\n", 0x5b);
        printf("Dec test: %d\n", 6296);

        // Stopping us from exiting kernel
        for (;;)
                ;
}
