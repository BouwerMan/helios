#include <kernel/memory/pmm.h>
#include <kernel/sys.h>
#include <stdio.h>

void pmm_init(struct limine_memmap_response* mmap)
{
    dputs("Reading Memory Map");
    uint64_t high_addr = 0;
    size_t total_len = 0;
    for (size_t i = 0; i < mmap->entry_count; i++) {
        struct limine_memmap_entry* entry = mmap->entries[i];
        printf("Start Addr: %x | Length: %x | Type: %d\n", entry->base, entry->length, entry->type);
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            high_addr = entry->base + entry->length;
            total_len += entry->length;
        }
    }
    dprintf("Highest address: 0x%x, Total memory length: %x\n", high_addr, total_len);
}
