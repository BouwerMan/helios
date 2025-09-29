#include "fs/vfs.h"
#include <arch/mmu/vmm.h>
#include <kernel/helios.h>
#include <kernel/tasks/scheduler.h>
#include <mm/mmap.h>
#include <mm/page_alloc.h>

static inline void* choose_base_addr(void* addr)
{
	if (!addr) {
		addr = DEF_ADDR;
	}

	struct address_space* vas = get_current_task()->vas;
	while (is_within_vas(vas, (vaddr_t)addr)) {
		addr = (void*)((uptr)addr + PAGE_SIZE);
	}
	return addr;
}

void* mmap_sys(void* addr,
	       size_t length,
	       int prot,
	       int flags,
	       int fd,
	       off_t offset)
{
	log_debug(
		"mmap called with parameters: addr=%p, length=%zu, prot=%d, flags=%d, fd=%d, offset=%zu",
		addr,
		length,
		prot,
		flags,
		fd,
		offset);
	if (length == 0) {
		log_error("mmap: length cannot be zero");
		return nullptr;
	}
	length = align_up_page(length);

	if (flags & MAP_ANONYMOUS) {
		addr = choose_base_addr(addr);

		int res = map_region(get_current_task()->vas,
				     (struct mr_file) { 0 },
				     (uptr)addr,
				     (uptr)addr + length,
				     (unsigned long)prot,
				     (unsigned long)flags);

		if (res < 0) {
			log_error("mmap: map_region failed");
			return nullptr;
		}

		return addr;
	}

	/*
	 * File-backed mapping
	 */

	if ((size_t)offset & (PAGE_SIZE - 1)) return nullptr; // -EINVAL
	struct vfs_file* file = get_file(fd);
	if (!file) {
		log_error("mmap: invalid file descriptor %d", fd);
		return nullptr; // -EBADF
	}

	addr = choose_base_addr(addr);

	kunimpl("File-backed mmap");

	return nullptr;
}

int munmap(void* addr, size_t length)
{
	(void)addr;
	(void)length;
	return 0;
}
