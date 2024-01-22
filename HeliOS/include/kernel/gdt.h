#ifndef _GDT_H
#define _GDT_H

#include <stddef.h>

void gdt_set_gate(int index, unsigned long base, unsigned long limit, unsigned char access, unsigned char gran);
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
        unsigned short limit;
        unsigned int base;
} __attribute__((packed));

#endif /* _GDT_H */
