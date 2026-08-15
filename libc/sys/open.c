#include "arch/syscall.h"
#include "errno.h"
#include "internal/fcntl.h"
#include "internal/features.h"

#include <fcntl.h>

// TODO: Support mode argument
int __open(const char* path, int oflag, ...)
{
	int fd = (int)__syscall2(SYS_OPEN, (long)path, (long)oflag);
	if (fd < 0) {
		errno = (int)-fd;
		return -1;
	}

	return fd;
}
weak_alias(__open, open);
