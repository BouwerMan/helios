#ifndef _GDT_H
#define _GDT_H

#include <stddef.h>
#include <stdint.h>

void gdt_set_gate(uint8_t index, uint64_t base, uint32_t limit, uint8_t access, uint8_t gran);
void gdt_init();

/* Defines a GDT entry. We say packed, because it prevents the
 *  compiler from doing things that it thinks is best: Prevent
 *  compiler "optimization" by packing */
struct gdt_entry {
    unsigned short limit_low;
    unsigned short base_low;
    unsigned char base_middle;
    unsigned char access;
    unsigned char granularity;
    unsigned char base_high;
} __attribute__((packed));

/* Special pointer which includes the limit: The max bytes
 *  taken up by the GDT, minus 1. Again, this NEEDS to be packed */
struct gdt_ptr {
    uint16_t limit;
    struct gdt_entry* offset;
} __attribute__((packed));

#endif /* _GDT_H */
