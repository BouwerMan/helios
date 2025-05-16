#include <kernel/screen.h>
#include <kernel/sys.h>
#include <util/log.h>

// Very rudimentary panic, still relies on libc and stuff.
void panic(char* message)
{
	__asm__ volatile("cli");
	set_log_mode(LOG_DIRECT);
	set_color(COLOR_RED, COLOR_BLACK);
	log_error("KERNEL PANIC!\n%s", message);
	for (;;)
		__asm__ volatile("hlt");
}
