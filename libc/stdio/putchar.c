#include <arch/syscall.h>
#include <printf.h>
#include <stdint.h>
#include <stdio.h>

int putchar(int ic)
{
	char c = (char)ic;
	// TODO: Implement stdio and the write system call.
	__syscall3(SYS_WRITE, 1, (long)&c, 1);
	return ic;
}

// Needed for printf lib
void putchar_(char c)
{
	putchar((int)c);
}
