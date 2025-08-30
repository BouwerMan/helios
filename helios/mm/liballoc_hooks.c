/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <kernel/spinlock.h>
#include <lib/log.h>
#include <mm/page_alloc.h>
#include <stddef.h>

static spinlock_t lock;
static unsigned long flags;

void liballoc_init()
{
	spin_init(&lock);
}

// Locks memory structures by disabling interrupts (really basic way)
int liballoc_lock()
{
	spin_lock_irqsave(&lock, &flags);
	return 0;
}

// Unlocks memory structures by enabling interrupts again (really basic way)
int liballoc_unlock()
{
	spin_unlock_irqrestore(&lock, flags);
	return 0;
}

// Returns and allocs [pages] number of contiguous pages
void* liballoc_alloc(size_t pages)
{
	log_debug("Allocating %zu pages", pages);
	void* alloc = get_free_pages(AF_KERNEL, pages);
	return alloc;
}

// Frees [pages] number of contiguous pages, starting at first_page
int liballoc_free(void* first_page, size_t pages)
{
	free_pages(first_page, pages);
	return 0;
}
