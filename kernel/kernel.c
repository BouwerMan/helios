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

        gdt_init();
        puts("GPT initialized(?)");

        idt_init();
        puts("IDT initialized(?)");

        isr_init();
        puts("ISRs initialized(?)");

        irq_init();
        puts("IRQs initialized(?)");

        timer_init();
        puts("PIT initialized(?)");

        keyboard_init();
        puts("Keyboard initialized(?)");

        // Stopping us from exiting kernel
        for (;;)
                ;
}
