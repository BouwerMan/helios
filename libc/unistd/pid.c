#include <arch/syscall.h>
#include <unistd.h>

pid_t getpid(void)
{
	return (pid_t)__syscall0(SYS_GETPID);
}

pid_t getppid(void)
{
	return (pid_t)__syscall0(SYS_GETPPID);
}
