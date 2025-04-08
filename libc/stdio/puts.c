#include <stdio.h>

#if defined(__is_libk)
#include <kernel/tty.h>

int puts(const char* string)
{
    screen_putstring(string);
    screen_putchar('\n');
    return 0;
}
#else
// TODO: Proper libc puts
int puts(const char* string) { }
#endif
