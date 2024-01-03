#include <kernel/tty.h>
#include <stddef.h>

void kernel_main(void)
{
    /* Initialize terminal interface */
    terminal_initialize();

    terminal_writestring("Hello, kernel World!\nNewline Test\n");
    for (size_t i = 0; i < 25 - 2; i++) {
        char test[3];
        test[0] = i + '0';
        test[1] = '\n';
        test[2] = '\0';
        terminal_writestring(test);
    }
    terminal_disable_cursor();
}
