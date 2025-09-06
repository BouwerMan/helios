#include <stdio.h>
void init_libc()
{
	// GDB BREAKPOINT
	__init_streams();

	printf("Initialized streams\n");
}
