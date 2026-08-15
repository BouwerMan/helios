#include <errno.h>
#include <sys/ioctl.h>

#include "arch/syscall.h"
#include "internal/features.h"

int __ioctl(int fd, unsigned long request, void* arg)
{
	long ret = __syscall3(SYS_IOCTL, (long)fd, (long)request, (long)arg);
	if (ret < 0) {
		errno = (int)-ret;
		return -1;
	}

	return (int)ret;
}
weak_alias(__ioctl, ioctl);
