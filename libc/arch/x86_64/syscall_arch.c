#include "arch/syscall.h"
#include <stddef.h>
#include <sys/types.h>

ssize_t __syscall_write(int fd, const void* buf, size_t count)
{
	return __syscall3(SYS_WRITE, fd, (long)buf, (long)count);
}

ssize_t __syscall_read(int fd, void* buf, size_t count)
{
	return __syscall3(SYS_READ, fd, (long)buf, (long)count);
}
