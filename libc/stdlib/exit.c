#include <arch/syscall.h>
#include <stdlib.h>

[[noreturn]]
void _exit(int status)
{
	__syscall1(SYS_EXIT, (long)status);
	while (1) {
		__builtin_ia32_pause();
	}
}

void exit(int status)
{
	_exit(status);
}
