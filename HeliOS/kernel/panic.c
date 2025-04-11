#include <kernel/screen.h>
#include <stdio.h>
#include <util/log.h>

// Very rudimentary panic, still relies on libc and stuff.
void panic(char* message)
{
    __asm__ volatile("cli");
    set_color(COLOR_RED, COLOR_BLACK);
    log_error("KERNEL PANIC!\n%s", message);
    for (;;)
        __asm__ volatile("hlt");
}
