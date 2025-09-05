#include <arch/syscall.h>
#include <unistd.h>

int execve(const char* path, char* const argv[], char* const envp[])
{
	return (int)__syscall3(SYS_EXEC, (long)path, (long)argv, (long)envp);
}
