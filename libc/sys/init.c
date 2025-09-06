#include <stdio.h>
void init_libc()
{
	__init_streams();

	printf("Initialized streams\n");
}
