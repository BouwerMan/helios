#include <gdt.h>
#include <interrupts.h>
#include <sys.h>
#include <timer.h>
#include <tty.h>

void terrible_version_print()
{
        terminal_writestring("Welcome to ");
        terminal_writestring(KERNEL_NAME);
        terminal_writestring(". Version: ");
        terminal_writestring(KERNEL_VERSION);
        terminal_writestring("\n");
}

void kernel_main(void)
{
        /* Initialize terminal interface */
        terminal_initialize();
        terminal_disable_cursor();
        terrible_version_print();

        gdt_init();
        terminal_writestring("GPT initialized(?)\n");

        idt_init();
        terminal_writestring("IDT initialized(?)\n");

        isr_init();
        terminal_writestring("ISRs initialized(?)\n");

        irq_init();
        terminal_writestring("IRQs initialized(?)\n");

        timer_init();
        terminal_writestring("PIT initialized(?)\n");

        // Stopping us from exiting kernel
        for (;;)
                ;
}
