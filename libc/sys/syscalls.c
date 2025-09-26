#include <internal/syscalls.h>

ssize_t __libc_write(int fd, const void* buf, size_t count)
{
	ssize_t res = __syscall_write(fd, buf, count);
	// TODO: errno handling
	return res;
}

ssize_t __libc_read(int fd, void* buf, size_t count)
{
	ssize_t res = __syscall_read(fd, buf, count);
	// TODO: errno handling
	return res;
}
