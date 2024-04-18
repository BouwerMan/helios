#pragma once
#include <kernel/sys.h>
#include <stdint.h>
/* This defines what the stack looks like after an ISR was running */
struct irq_regs {
    unsigned int gs, fs, es, ds; /* pushed the segs last */
    unsigned int edi, esi, ebp, esp, ebx, edx, ecx, eax; /* pushed by 'pusha' */
    unsigned int int_no, err_code; /* our 'push byte #' and ecodes do this */
    unsigned int eip, cs, eflags, useresp, ss; /* pushed by the processor automatically */
};

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
    uintptr_t base;
} __attribute__((packed));

// idt.c
void idt_set_gate(unsigned char, unsigned long, unsigned short, unsigned char);
void idt_init();

// isr.c
void install_isr_handler(int isr, void (*handler)(struct irq_regs* r));
void uninstall_isr_handler(int isr);
void isr_init();
void fault_handler(struct irq_regs*);

// irq.c
void irq_handler(struct irq_regs* r);
void irq_init();
void irq_remap(void);
void irq_uninstall_handler(int irq);
void irq_install_handler(int irq, void (*handler)(struct irq_regs* r));
