#include "arch/syscall.h"
#include "unistd.h"

int access(const char* path, int amode)
{
	return (int)__syscall2(SYS_ACCESS, (long)path, amode);
}
