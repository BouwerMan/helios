#include <stdio.h>

#if defined(__is_libk)
#include <kernel/tty.h>

int puts(const char* string)
{
    tty_writestring(string);
    tty_putchar('\n');
    return 0;
}
#else
// TODO: Proper libc puts
int puts(const char* string)
{
}
#endif
