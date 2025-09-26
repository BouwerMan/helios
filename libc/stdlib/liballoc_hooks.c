/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stddef.h>
#include <stdlib.h>
#include <sys/mman.h>

void liballoc_init()
{
}

// Locks memory structures by disabling interrupts (really basic way)
int liballoc_lock()
{
	return 0;
}

// Unlocks memory structures by enabling interrupts again (really basic way)
int liballoc_unlock()
{
	return 0;
}

// Returns and allocs [pages] number of contiguous pages
void* liballoc_alloc(size_t pages)
{
	void* alloc = mmap(nullptr,
			   pages * 4096,
			   PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_ANONYMOUS,
			   -1,
			   0);
	return alloc;
}

// Frees [pages] number of contiguous pages, starting at first_page
int liballoc_free(void* first_page, size_t pages)
{
	(void)first_page;
	(void)pages;
	// free syscall
	return 0;
}
