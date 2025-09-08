#include "arch/syscall.h"
#include "unistd.h"
#include <errno.h>

char* getcwd(char* buf, size_t size)
{
	void* res = (void*)__syscall2(SYS_GETCWD, (long)buf, (long)size);
	if (!res) {
		errno = ENOMEM;
	}
	return res;
}

int chdir(const char* path)
{
	long res = __syscall1(SYS_CHDIR, (long)path);
	if (res < 0) {
		errno = (int)-res;
	}
	return (int)res;
}
