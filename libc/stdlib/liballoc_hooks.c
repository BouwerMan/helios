/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stddef.h>
#include <stdlib.h>

#if defined(__is_libk)
#include <kernel/spinlock.h>
#include <mm/page_alloc.h>

spinlock_t lock;
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
// __asm__ volatile("cli");
#if defined(__is_libk)
	spinlock_acquire(&lock);
#endif
	return 0;
}

// Unlocks memory structures by enabling interrupts again (really basic way)
int liballoc_unlock()
{
// __asm__ volatile("sti");
#if defined(__is_libk)
	spinlock_release(&lock);
#endif
	return 0;
}

// Returns and allocs [pages] number of contiguous pages
void* liballoc_alloc(size_t pages)
{
#if defined(__is_libk)
	return (void*)get_free_pages(AF_KERNEL, pages);
#else
	// sbrk syscall
	(void)pages;
	return nullptr;
#endif
}

// Frees [pages] number of contiguous pages, starting at first_page
int liballoc_free(void* first_page, size_t pages)
{
#if defined(__is_libk)
	free_pages(first_page, pages);
#else
	(void)first_page;
	(void)pages;
	// free syscall
#endif
	return 0;
}
