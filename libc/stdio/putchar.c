#include <printf.h>
#include <stdio.h>

#if defined(__is_libk)
#include <drivers/serial.h>
#include <kernel/screen.h>
#endif

int putchar(int ic)
{
#if defined(__is_libk)
	char c = (char)ic;
	screen_putchar(c);
#else
	// TODO: Implement stdio and the write system call.
#endif
	return ic;
}

// Needed for printf lib
void putchar_(char c)
{
	screen_putchar(c);
}
