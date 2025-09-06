#include "arch/syscall.h"
#include "unistd.h"

// TODO: Make __syscall_* wrappers

char* getcwd(char* buf, size_t size)
{
	void* res = (void*)__syscall2(SYS_GETCWD, (long)buf, (long)size);
	if (!res) {
		// TODO: set errno
	}
	return res;
}

int chdir(const char* path)
{
	long res = __syscall1(SYS_CHDIR, (long)path);
	if (res < 0) {
		// TODO: set errno
	}
	return (int)res;
}
