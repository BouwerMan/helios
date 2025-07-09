#include <arch/mmu/vmm.h>
#include <kernel/helios.h>
#include <mm/mmap.h>
#include <mm/page_alloc.h>

// Because we are kernel I am going to force use to specify a PML4
void* mmap_sys(u64* pml4, void* addr, size_t length, int prot, int flags, int fd, size_t offset)
{
	log_debug("mmap called with parameters: addr=%p, length=%zu, prot=%d, flags=%d, fd=%d, offset=%zu", addr,
		  length, prot, flags, fd, offset);

	if (!addr) {
		addr = DEF_ADDR;
	}

	size_t pages = CEIL_DIV(length, PAGE_SIZE);
	uptr paddr = HHDM_TO_PHYS(get_free_pages(AF_KERNEL, pages));

	// TODO: More flags???
	for (size_t i = 0; i < pages; i++) {
		int res = vmm_map_page(pml4, (uptr)addr + i * PAGE_SIZE, paddr + i * PAGE_SIZE,
				       PAGE_PRESENT | PAGE_WRITE);
		if (res) return MAP_FAILED;
		log_debug("Mapped vaddr: %p, to paddr: %lx", addr, paddr);
	}

	return addr;
}

int munmap(void* addr, size_t length)
{
	return 0;
}
