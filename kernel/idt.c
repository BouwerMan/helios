// TODO: Should this be only in x86_64 arch?
#include <idt.h>
#include <string.h>

/* Declare an IDT of 256 entries. Although we will only use the
 *  first 32 entries in this tutorial, the rest exists as a bit
 *  of a trap. If any undefined IDT entry is hit, it normally
 *  will cause an "Unhandled Interrupt" exception. Any descriptor
 *  for which the 'presence' bit is cleared (0) will generate an
 *  "Unhandled Interrupt" exception */
struct idt_entry idt[256];
struct idt_ptr idtp;

/* This exists in 'boot.asm', and is used to load our IDT */
extern void idt_load();

/* Use this function to set an entry in the IDT. Alot simpler
 *  than twiddling with the GDT ;) */
void idt_set_gate(unsigned char index, unsigned long base, unsigned short sel, unsigned char flags)
{
    idt[index].base_lo = (base & 0xFFFF);
    idt[index].base_hi = (base >> 16) & 0xFFFF;

    idt[index].sel = sel;
    idt[index].flags = flags;
    idt[index].always0 = 0;
}

/* Installs the IDT */
void idt_init()
{
    /* Sets the special IDT pointer up, just like in 'gdt.c' */
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base = (unsigned int)&idt;

    /* Clear out the entire IDT, initializing it to zeros */
    memset(&idt, 0, sizeof(struct idt_entry) * 256);

    /* Add any new ISRs to the IDT here using idt_set_gate */

    /* Points the processor's internal register to the new IDT */
    idt_load();
}
