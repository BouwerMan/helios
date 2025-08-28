#include <arch/mmu/vmm.h>
#include <kernel/helios.h>
#include <kernel/tasks/scheduler.h>
#include <mm/mmap.h>
#include <mm/page_alloc.h>

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

	if (!(flags & MAP_ANONYMOUS)) {
		log_error("mmap: Only MAP_ANONYMOUS is supported currently");
		return nullptr;
	}

	if (!addr) {
		addr = DEF_ADDR;
	}

	struct address_space* vas = get_current_task()->vas;
	while (is_within_vas(vas, (vaddr_t)addr)) {
		addr += PAGE_SIZE;
	}

	int res = map_region(get_current_task()->vas,
			     (uptr)addr,
			     (uptr)addr + length,
			     (unsigned long)prot,
			     (unsigned long)flags);

	if (res < 0) {
		log_error("mmap: map_region failed");
		return nullptr;
	}
	// size_t pages = CEIL_DIV(length, PAGE_SIZE);
	// uptr paddr = HHDM_TO_PHYS(get_free_pages(AF_KERNEL, pages));
	//
	// // TODO: More flags???
	// for (size_t i = 0; i < pages; i++) {
	// 	int res = vmm_map_page((pgd_t*)pml4,
	// 			       (uptr)addr + i * PAGE_SIZE,
	// 			       paddr + i * PAGE_SIZE,
	// 			       PAGE_PRESENT | PAGE_WRITE);
	// 	if (res) return MAP_FAILED;
	// 	log_debug("Mapped vaddr: %p, to paddr: %lx", addr, paddr);
	// }

	return addr;
}

int munmap(void* addr, size_t length)
{
	return 0;
}
