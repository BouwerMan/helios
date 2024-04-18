#include <kernel/memory.h>
#include <stddef.h>

// Locks memory structures by disabling interrupts (really basic way)
int liballoc_lock()
{
    asm volatile("cli");
    return 0;
}

// Unlocks memory structures by enabling interrupts again (really basic way)
int liballoc_unlock()
{
    asm volatile("sti");
    return 0;
}

// Returns and allocs [pages] number of contiguous pages
void* liballoc_alloc(size_t pages) { return (void*)kalloc_frames(pages); }

// Frees [pages] number of contiguous pages, starting at first_page
int liballoc_free(void* first_page, size_t pages)
{
    kfree_frames((uintptr_t)first_page, pages);
    return 0;
}
