#include <arch/syscall.h>
#include <helios/syscall.h>
#include <stdint.h>
#include <sys/mman.h>

void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset)
{
	__syscall6(SYS_MMAP, (uintptr_t)addr, length, prot, flags, fd, offset);
}
