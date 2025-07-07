#include <printf.h>
#include <stdio.h>

#if defined(__is_libk)
#include <drivers/serial.h>
#include <kernel/dmesg.h>
#include <kernel/screen.h>
#else
extern void writec(char* c);
#endif

int putchar(int ic)
{
#if defined(__is_libk)
	char c = (char)ic;
	screen_putchar(c);
	write_serial(c);
#else
	// TODO: Implement stdio and the write system call.
#endif
	return ic;
}

// Needed for printf lib
void putchar_(char c)
{
#if defined(__is_libk)
	screen_putchar(c);
	write_serial(c);
#else
	// TODO: syscalls
	writec(&c);
#endif
}
