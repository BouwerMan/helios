#include "drivers/serial.h"
#include "drivers/term.h"
#include "lib/printf.h"

int putchar(int ic)
{
	char c = (char)ic;
	term_putchar(c);
	write_serial(c);
	return ic;
}

// Needed for printf lib
void putchar_(char c) __attribute__((alias("putchar")));
