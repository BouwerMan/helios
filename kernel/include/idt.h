#ifndef _IDT_H
#define _IDT_H
/* Defines an IDT entry */
struct idt_entry {
    unsigned short base_lo;
    unsigned short sel; /* Our kernel segment goes here! */
    unsigned char always0; /* This will ALWAYS be set to 0! */
    unsigned char flags; /* Set using the above table! */
    unsigned short base_hi;
} __attribute__((packed));

struct idt_ptr {
    unsigned short limit;
    unsigned int base;
} __attribute__((packed));

void idt_set_gate(unsigned char, unsigned long, unsigned short, unsigned char);
void idt_init();

#endif /* _IDT_H */
