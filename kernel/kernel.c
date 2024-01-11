#include <gdt.h>
#include <interrupts.h>
#include <stdio.h>
#include <sys.h>
#include <timer.h>
#include <tty.h>

void kernel_main(void)
{
        /* Initialize terminal interface */
        terminal_initialize();
        terminal_disable_cursor();
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

        // Stopping us from exiting kernel
        for (;;)
                ;
}
