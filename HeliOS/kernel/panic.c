#include <kernel/sys.h>
#include <kernel/tty.h>
#include <stdio.h>

// Very rudimentary panic, still relies on libc and stuff.
void panic(char* message)
{
    // asm volatile("cli");
    // tty_setcolor(VGA_COLOR_RED);
    // puts("KERNEL PANIC!");
    // puts(message);
    for (;;)
        asm volatile("hlt");
}
