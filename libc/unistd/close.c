#include "arch/syscall.h"
#include "errno.h"
#include "internal/features.h"

int __close(int fd)
{
	if (fd < 0) {
		errno = EBADF;
		return -1;
	}

	int ret = (int)__syscall1(SYS_CLOSE, (long)fd);
	if (ret < 0) {
		errno = -ret;
		return -1;
	}

	return ret;
}
weak_alias(__close, close);
