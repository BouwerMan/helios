#include <arch/syscall.h>
#include <helios/syscall.h>
#include <stdint.h>
#include <sys/mman.h>

void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset)
{
	return (void*)__syscall6(
		SYS_MMAP, (long)addr, (long)length, prot, flags, fd, offset);
}
