#include "arch/syscall.h"
#include "internal/features.h"
#include "internal/unistd.h"

pid_t __getpid(void)
{
	return (pid_t)__syscall0(SYS_GETPID);
}
weak_alias(__getpid, getpid);

pid_t __getppid(void)
{
	return (pid_t)__syscall0(SYS_GETPPID);
}
weak_alias(__getppid, getppid);
