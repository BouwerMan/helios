#include <arch/syscall.h>
#include <stdlib.h>

[[noreturn]]
void _exit(int status)
{
	__syscall1(SYS_EXIT, (uintptr_t)status);
	while (1) {
		__builtin_ia32_pause();
	}
}

void exit(int status)
{
#ifdef __is_libk
	(void)status;
	while (1) {
		__builtin_ia32_pause();
	}
#else
	_exit(status);
#endif
}
