#include "arch/syscall.h"
#include "internal/features.h"
#include "internal/unistd.h"

pid_t __fork(void)
{
	return (pid_t)__syscall0(SYS_FORK);
}
weak_alias(__fork, fork);
