#include <arch/syscall.h>
#include <unistd.h>

pid_t fork(void)
{
	return (pid_t)__syscall0(SYS_FORK);
}
