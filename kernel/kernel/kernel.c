#include <kernel/tty.h>
#include <stddef.h>

void kernel_main(void)
{
    /* Initialize terminal interface */
    terminal_initialize();
    terminal_disable_cursor();
    terminal_writestring("Welcome to HELIOS!\n");
}
