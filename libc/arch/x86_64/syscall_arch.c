#include "arch/syscall.h"
#include <stddef.h>
#include <sys/types.h>

ssize_t __syscall_write(int fd, const void* buf, size_t count)
{
	return __syscall3(SYS_WRITE, fd, (long)buf, (long)count);
}
