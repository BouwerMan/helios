#include <gdt.h>
#include <idt.h>
#include <isr.h>
#include <sys.h>
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

    // Test for ISRs
    // Creates an invalid opcode exception when -O2 is set?
    int test = 10 / 0;
    terminal_writestring((char*)test);
}
