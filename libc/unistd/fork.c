#include <arch/syscall.h>
#include <unistd.h>

pid_t fork(void)
{
	return __syscall0(SYS_FORK);
}
