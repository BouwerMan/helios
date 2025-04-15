#include <kernel/memory/vmm.h>
#include <stddef.h>
#include <util/log.h>

// Locks memory structures by disabling interrupts (really basic way)
int liballoc_lock()
{
	__asm__ volatile("cli");
	return 0;
}

// Unlocks memory structures by enabling interrupts again (really basic way)
int liballoc_unlock()
{
	__asm__ volatile("sti");
	return 0;
}

// Returns and allocs [pages] number of contiguous pages
void* liballoc_alloc(size_t pages)
{
	return vmm_alloc_pages(pages);
}

// Frees [pages] number of contiguous pages, starting at first_page
// TODO: actually free pages
int liballoc_free(void* first_page, size_t pages)
{
	vmm_free_pages(first_page, pages);
	return 0;
}
