#include "internal/syscalls.h"
#include "unistd.h"

ssize_t read(int fd, void* buf, size_t count)
{
	return __libc_read(fd, buf, count);
}
