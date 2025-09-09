#include "arch/syscall.h"
#include "internal/features.h"
#include "internal/unistd.h"
#include <asm/syscall.h>
#include <errno.h>

ssize_t __write(int fd, const void* buf, size_t count)
{
	if (!buf || count == 0) {
		errno = EINVAL;
		return -1;
	}

	ssize_t ret = __syscall3(SYS_WRITE, fd, (long)buf, (long)count);

	if (ret < 0) {
		errno = (int)-ret;
		return -1;
	}

	return ret;
}
weak_alias(__write, write);
