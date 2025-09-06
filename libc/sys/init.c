#include <stdio.h>

char** environ; // POSIX standard global

void __init_libc(int argc, char** argv, char** envp)
{
	__init_streams();
	environ = envp;
}
