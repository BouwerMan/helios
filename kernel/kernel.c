#include <gdt.h>
#include <interrupts.h>
#include <keyboard.h>
#include <stdio.h>
#include <sys.h>
#include <timer.h>
#include <tty.h>

void kernel_main(void)
{
        /* Initialize terminal interface */
        tty_initialize();
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

        // Stopping us from exiting kernel
        for (;;)
                ;
}
