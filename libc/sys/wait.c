#include <arch/syscall.h>
#include <sys/wait.h>

pid_t waitpid(pid_t pid, int* stat_loc, int options)
{
	return (pid_t)__syscall3(
		SYS_WAITPID, (long)pid, (long)stat_loc, (long)options);
}

// wait() is just waitpid() with specific arguments
pid_t wait(int* stat_loc)
{
	return waitpid(-1, stat_loc, 0);
}
