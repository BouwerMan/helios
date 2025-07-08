#include <printf.h>
#include <stdint.h>
#include <stdio.h>

#if defined(__is_libk)
#include <drivers/serial.h>
#include <kernel/dmesg.h>
#include <kernel/screen.h>
#else
#include <arch/syscall.h>
#endif

int putchar(int ic)
{
	char c = (char)ic;
#if defined(__is_libk)
	screen_putchar(c);
	write_serial(c);
#else
	// TODO: Implement stdio and the write system call.
	__syscall3(SYS_WRITE, 1, (long)&c, 1);
#endif
	return ic;
}

// Needed for printf lib
void putchar_(char c) __attribute__((alias("putchar")));
