#include <gdt.h>
#include <stddef.h>
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
}
