/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <kernel/kmath.h>
#include <stddef.h>

#include <kernel/liballoc.h>
#include <kernel/spinlock.h>
#include <mm/page_alloc.h>

#include <util/log.h>

spinlock_t lock;

void liballoc_init()
{
	spinlock_init(&lock);
}

// Locks memory structures by disabling interrupts (really basic way)
int liballoc_lock()
{
	// __asm__ volatile("cli");
	spinlock_acquire(&lock);
	return 0;
}

// Unlocks memory structures by enabling interrupts again (really basic way)
int liballoc_unlock()
{
	// __asm__ volatile("sti");
	spinlock_release(&lock);
	return 0;
}

// Returns and allocs [pages] number of contiguous pages
void* liballoc_alloc(size_t pages)
{
	return (void*)get_free_pages(AF_KERNEL, pages);
}

// Frees [pages] number of contiguous pages, starting at first_page
int liballoc_free(void* first_page, size_t pages)
{
	free_pages(first_page, pages);
	return 0;
}
