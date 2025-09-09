#include "arch/syscall.h"
#include "internal/stdio.h"
#include "internal/stdlib.h"

[[noreturn]]
void _exit(int status)
{
	__syscall1(SYS_EXIT, (long)status);
	while (1) {
		__syscall1(SYS_EXIT, (long)status);
	}
}

void exit(int status)
{
	__cleanup_streams();
	_exit(status);
}
