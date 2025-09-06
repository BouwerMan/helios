#include <internal/syscalls.h>

ssize_t __libc_write(int fd, const void* buf, size_t count)
{
	ssize_t res = __syscall_write(fd, buf, count);
	// TODO: errno handling
	return res;
}
