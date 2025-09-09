#include "arch/syscall.h"
#include "internal/features.h"
#include "internal/unistd.h"

int __access(const char* path, int amode)
{
	return (int)__syscall2(SYS_ACCESS, (long)path, amode);
}
weak_alias(__access, access);
