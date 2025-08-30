#include <arch/syscall.h>
#include <unistd.h>

// Replaces the current process with an image from a loaded Limine module.
// This function does not return on success.
int exec_module(const char* module_name)
{
	// Returns -1 on failure
	return (int)__syscall1(SYS_EXEC, (long)module_name);
}

int execv(const char*, char* const[])
{
}
