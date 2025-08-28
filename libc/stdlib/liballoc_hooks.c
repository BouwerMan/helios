/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stddef.h>
#include <stdlib.h>

#if defined(__is_libk)
#include <kernel/spinlock.h>
#include <mm/page_alloc.h>
#include <util/log.h>

static spinlock_t lock;
static unsigned long flags;
#else
#include <sys/mman.h>
#endif

void liballoc_init()
{
#if defined(__is_libk)
	spinlock_init(&lock);
#endif
}

// Locks memory structures by disabling interrupts (really basic way)
int liballoc_lock()
{
#if defined(__is_libk)
	spin_lock_irqsave(&lock, &flags);
#endif
	return 0;
}

// Unlocks memory structures by enabling interrupts again (really basic way)
int liballoc_unlock()
{
#if defined(__is_libk)
	spin_unlock_irqrestore(&lock, flags);
#endif
	return 0;
}

// Returns and allocs [pages] number of contiguous pages
void* liballoc_alloc(size_t pages)
{
#if defined(__is_libk)
	void* alloc = get_free_pages(AF_KERNEL, pages);
	return alloc;
#else
	void* alloc = mmap(nullptr,
			   pages * 4096,
			   PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_ANONYMOUS,
			   -1,
			   0);
	return alloc;
#endif
}

// Frees [pages] number of contiguous pages, starting at first_page
int liballoc_free(void* first_page, size_t pages)
{
#if defined(__is_libk)
	// log_debug("liballoc_free: Freeing %zu pages at %p", pages, first_page);
	free_pages(first_page, pages);
#else
	(void)first_page;
	(void)pages;
	// free syscall
#endif
	return 0;
}
