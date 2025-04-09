#include "idt.h"
#include "../ports.h"
#include <stdio.h>
#include <string.h>

/* To print the message which defines every exception */
const char* exception_messages[] = {
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Into Detected Overflow",
    "Out of Bounds",
    "Invalid Opcode",
    "No Coprocessor",

    "Double Fault",
    "Coprocessor Segment Overrun",
    "Bad TSS",
    "Segment Not Present",
    "Stack Fault",
    "General Protection Fault",
    "Page Fault",
    "Unknown Interrupt",

    "Coprocessor Fault",
    "Alignment Check",
    "Machine Check",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",

    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
};

static void default_exception_handler(struct registers* registers)
{
    printf("Recieved interrupt #%x with error code %x on the default handler!\n", registers->int_no,
           registers->err_code);
    printf("Exception: %s\n", exception_messages[registers->int_no]);
    printf("RIP: %x, RSP: %x, RBP: %x\n", registers->rip, registers->rsp, registers->rbp);
    printf("RAX: %x, RBX: %x, RCX: %x, RDX: %x\n", registers->rax, registers->rbx, registers->rcx, registers->rdx);
    printf("RDI: %x, RSI: %x, RFLAGS: %x, DS: %x\n", registers->rdi, registers->rsi, registers->rflags, registers->ds);
    printf("CS: %x, SS: %x\n", registers->cs, registers->ss);
    printf("R8: %x, R9: %x, R10: %x, R11: %x\n", registers->r8, registers->r9, registers->r10, registers->r11);
    printf("R12: %x, R13: %x, R14: %x, R15: %x\n", registers->r12, registers->r13, registers->r14, registers->r15);

    __asm__ volatile("cli; hlt");
}

__attribute__((aligned(0x10))) static idt_entry_t idt[256]; // Create an array of IDT entries; aligned for performance
static idtr_t idtr;

void idt_set_descriptor(uint8_t vector, uint64_t isr, uint8_t flags)
{
    idt_entry_t* descriptor = &idt[vector];

    descriptor->isr_low = (uint16_t)(isr & 0xFFFF);
    descriptor->kernel_cs = KERNEL_CS;
    descriptor->ist = 0;
    descriptor->attributes = flags;
    descriptor->isr_mid = (uint16_t)((isr & 0x00000000FFFF0000) >> 16);
    descriptor->isr_high = (uint32_t)((isr & 0xFFFFFFFF00000000) >> 32);
    descriptor->reserved = 0;
}

void idt_init()
{
    idtr.base = (uintptr_t)&idt[0];
    idtr.limit = (uint16_t)sizeof(idt_entry_t) * IDT_ENTRIES - 1;

    /* Clear out the entire IDT, initializing it to zeros */
    memset((unsigned char*)&idt, 0, sizeof(idt_entry_t) * 256);

    isr_init();
    irq_init();

    __asm__ volatile("lidt %0" : : "m"(idtr)); // load the new IDT

    // Initialise the PIC and remap funny irqs
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();

    outb(PIC1_DATA, 0x20);
    io_wait();
    outb(PIC2_DATA, 0x28);
    io_wait();

    outb(PIC1_DATA, 4);
    io_wait();
    outb(PIC2_DATA, 2);
    io_wait();

    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    // Unmask the PICs
    outb(PIC1_DATA, 0x0);
    io_wait();
    outb(PIC2_DATA, 0x0);

    __asm__ volatile("sti"); // set the interrupt flag
}

// ISR Stuff
static void* interrupt_handlers[256] = { NULL };

void install_isr_handler(int isr, void (*handler)(struct registers* r)) { interrupt_handlers[isr] = handler; }

void uninstall_isr_handler(int isr) { interrupt_handlers[isr] = 0; }

/* These are function prototypes for all of the exception
 *  handlers: The first 32 entries in the IDT are reserved
 *  by Intel, and are designed to service exceptions! */
/* This is a very repetitive function... it's not hard, it's
 *  just annoying. As you can see, we set the first 32 entries
 *  in the IDT to the first 32 ISRs. We can't use a for loop
 *  for this, because there is no way to get the function names
 *  that correspond to that given entry. We set the access
 *  flags to 0x8E. This means that the entry is present, is
 *  running in ring 0 (kernel level), and has the lower 5 bits
 *  set to the required '14', which is represented by 'E' in
 *  hex. */
void isr_init()
{
    idt_set_descriptor(0, (uint64_t)isr0, 0x8E);
    idt_set_descriptor(1, (uint64_t)isr1, 0x8E);
    idt_set_descriptor(2, (uint64_t)isr2, 0x8E);
    idt_set_descriptor(3, (uint64_t)isr3, 0x8E);
    idt_set_descriptor(4, (uint64_t)isr4, 0x8E);
    idt_set_descriptor(5, (uint64_t)isr5, 0x8E);
    idt_set_descriptor(6, (uint64_t)isr6, 0x8E);
    idt_set_descriptor(7, (uint64_t)isr7, 0x8E);
    idt_set_descriptor(8, (uint64_t)isr8, 0x8E);
    idt_set_descriptor(9, (uint64_t)isr9, 0x8E);
    idt_set_descriptor(10, (uint64_t)isr10, 0x8E);
    idt_set_descriptor(11, (uint64_t)isr11, 0x8E);
    idt_set_descriptor(12, (uint64_t)isr12, 0x8E);
    idt_set_descriptor(13, (uint64_t)isr13, 0x8E);
    idt_set_descriptor(14, (uint64_t)isr14, 0x8E);
    idt_set_descriptor(15, (uint64_t)isr15, 0x8E);
    idt_set_descriptor(16, (uint64_t)isr16, 0x8E);
    idt_set_descriptor(17, (uint64_t)isr17, 0x8E);
    idt_set_descriptor(18, (uint64_t)isr18, 0x8E);
    idt_set_descriptor(19, (uint64_t)isr19, 0x8E);
    idt_set_descriptor(20, (uint64_t)isr20, 0x8E);
    idt_set_descriptor(21, (uint64_t)isr21, 0x8E);
    idt_set_descriptor(22, (uint64_t)isr22, 0x8E);
    idt_set_descriptor(23, (uint64_t)isr23, 0x8E);
    idt_set_descriptor(24, (uint64_t)isr24, 0x8E);
    idt_set_descriptor(25, (uint64_t)isr25, 0x8E);
    idt_set_descriptor(26, (uint64_t)isr26, 0x8E);
    idt_set_descriptor(27, (uint64_t)isr27, 0x8E);
    idt_set_descriptor(28, (uint64_t)isr28, 0x8E);
    idt_set_descriptor(29, (uint64_t)isr29, 0x8E);
    idt_set_descriptor(30, (uint64_t)isr30, 0x8E);
    idt_set_descriptor(31, (uint64_t)isr31, 0x8E);

    // Set default handler for isr
    for (uint8_t i = 0; i < 32; i++) {
        install_isr_handler(i, default_exception_handler);
    }
}

/* All of our Exception handling Interrupt Service Routines will
 *  point to this function. This will tell us what exception has
 *  happened! Right now, we simply halt the system by hitting an
 *  endless loop. All ISRs disable interrupts while they are being
 *  serviced as a 'locking' mechanism to prevent an IRQ from
 *  happening and messing up kernel data structures */
void isr_handler(struct registers* r)
{
    /* Is this a fault whose number is from 0 to 31? */
    if (r->int_no < 32) {
        void (*handler)(struct registers* r);
        handler = interrupt_handlers[r->int_no];
        /* Display the description for the Exception that occurred.
         *  In this tutorial, we will simply halt the system using an
         *  infinite loop */
        if (handler)
            handler(r);
        else {
            puts((char*)exception_messages[r->int_no]);
            puts(" Exception. System Halted!\n");
            for (;;)
                ;
        }
    }
}

// IRQ stuff
void irq_init(void)
{
    idt_set_descriptor(32, (uint64_t)irq0, 0x8E);
    idt_set_descriptor(33, (uint64_t)irq1, 0x8E);
    idt_set_descriptor(34, (uint64_t)irq2, 0x8E);
    idt_set_descriptor(35, (uint64_t)irq3, 0x8E);
    idt_set_descriptor(36, (uint64_t)irq4, 0x8E);
    idt_set_descriptor(37, (uint64_t)irq5, 0x8E);
    idt_set_descriptor(38, (uint64_t)irq6, 0x8E);
    idt_set_descriptor(39, (uint64_t)irq7, 0x8E);
    idt_set_descriptor(40, (uint64_t)irq8, 0x8E);
    idt_set_descriptor(41, (uint64_t)irq9, 0x8E);
    idt_set_descriptor(42, (uint64_t)irq10, 0x8E);
    idt_set_descriptor(43, (uint64_t)irq11, 0x8E);
    idt_set_descriptor(44, (uint64_t)irq12, 0x8E);
    idt_set_descriptor(45, (uint64_t)irq13, 0x8E);
    idt_set_descriptor(46, (uint64_t)irq14, 0x8E);
    idt_set_descriptor(47, (uint64_t)irq15, 0x8E);
}

/* Each of the IRQ ISRs point to this function, rather than
 *  the 'fault_handler' in 'isrs.c'. The IRQ Controllers need
 *  to be told when you are done servicing them, so you need
 *  to send them an "End of Interrupt" command (0x20). There
 *  are two 8259 chips: The first exists at 0x20, the second
 *  exists at 0xA0. If the second controller (an IRQ from 8 to
 *  15) gets an interrupt, you need to acknowledge the
 *  interrupt at BOTH controllers, otherwise, you only send
 *  an EOI command to the first controller. If you don't send
 *  an EOI, you won't raise any more IRQs */
void irq_handler(struct registers* r)
{
    /* This is a blank function pointer */
    void (*handler)(struct registers* r);

    /* Find out if we have a custom handler to run for this
     *  IRQ, and then finally, run it */
    handler = interrupt_handlers[r->int_no];
    if (handler) {
        handler(r);
    }

    /* If the IDT entry that was invoked was greater than 40
     *  (meaning IRQ8 - 15), then we need to send an EOI to
     *  the slave controller */
    if (r->int_no >= 40) {
        outb(0xA0, 0x20);
    }

    /* In either case, we need to send an EOI to the master
     *  interrupt controller too */
    outb(0x20, 0x20);
}

void IRQ_set_mask(uint8_t IRQline)
{
    uint16_t port;
    uint8_t value;

    if (IRQline < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        IRQline -= 8;
    }
    value = inb(port) | (1 << IRQline);
    outb(port, value);
}

void IRQ_clear_mask(uint8_t IRQline)
{
    uint16_t port;
    uint8_t value;

    if (IRQline < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        IRQline -= 8;
    }
    value = inb(port) & ~(1 << IRQline);
    outb(port, value);
}
