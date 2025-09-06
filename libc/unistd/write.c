#include "internal/syscalls.h"
#include "unistd.h"

ssize_t write(int fd, const void* buf, size_t count)
{
	return __libc_write(fd, buf, count);
}
