#include <kernel/sys.h>
#include <stdio.h>

// Very rudimentary panic, still relies on libc and stuff.
void panic(char* message)
{
        asm volatile("cli");
        puts("PANIC PANIC AAAAAAAAAAAAAAAAAAAAA");
        puts(message);
        asm volatile("hlt");
        for (;;)
                ;
}
