/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stddef.h>

#include <kernel/liballoc.h>
#include <kernel/memory/vmm.h>
#include <kernel/spinlock.h>

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
	return vmm_alloc_pages(pages, false);
}

// Frees [pages] number of contiguous pages, starting at first_page
int liballoc_free(void* first_page, size_t pages)
{
	vmm_free_pages(first_page, pages);
	return 0;
}
