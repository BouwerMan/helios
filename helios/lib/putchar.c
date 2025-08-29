#include <drivers/serial.h>
#include <kernel/screen.h>
#include <lib/printf.h>

int putchar(int ic)
{
	char c = (char)ic;
	screen_putchar(c);
	write_serial(c);
	return ic;
}

// Needed for printf lib
void putchar_(char c) __attribute__((alias("putchar")));
