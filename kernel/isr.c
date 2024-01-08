#include <idt.h>
#include <isr.h>
#include <string.h>
#include <tty.h>

/* These are function prototypes for all of the exception
 *  handlers: The first 32 entries in the IDT are reserved
 *  by Intel, and are designed to service exceptions! */
void extern isr0();
void extern isr1();
void extern isr2();
void extern isr3();
void extern isr4();
void extern isr5();
void extern isr6();
void extern isr7();
void extern isr8();
void extern isr9();
void extern isr10();
void extern isr11();
void extern isr12();
void extern isr13();
void extern isr14();
void extern isr15();
void extern isr16();
void extern isr17();
void extern isr18();
void extern isr19();
void extern isr20();
void extern isr21();
void extern isr22();
void extern isr23();
void extern isr24();
void extern isr25();
void extern isr26();
void extern isr27();
void extern isr28();
void extern isr29();
void extern isr30();
void extern isr31();

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
    idt_set_gate(0, (unsigned)isr0, 0x08, 0x8E);
    idt_set_gate(1, (unsigned)isr1, 0x08, 0x8E);
    idt_set_gate(2, (unsigned)isr2, 0x08, 0x8E);
    idt_set_gate(3, (unsigned)isr3, 0x08, 0x8E);
    idt_set_gate(4, (unsigned)isr4, 0x08, 0x8E);
    idt_set_gate(5, (unsigned)isr5, 0x08, 0x8E);
    idt_set_gate(6, (unsigned)isr6, 0x08, 0x8E);
    idt_set_gate(7, (unsigned)isr7, 0x08, 0x8E);
    idt_set_gate(8, (unsigned)isr8, 0x08, 0x8E);
    idt_set_gate(9, (unsigned)isr9, 0x08, 0x8E);
    idt_set_gate(10, (unsigned)isr10, 0x08, 0x8E);
    idt_set_gate(11, (unsigned)isr11, 0x08, 0x8E);
    idt_set_gate(12, (unsigned)isr12, 0x08, 0x8E);
    idt_set_gate(13, (unsigned)isr13, 0x08, 0x8E);
    idt_set_gate(14, (unsigned)isr14, 0x08, 0x8E);
    idt_set_gate(15, (unsigned)isr15, 0x08, 0x8E);
    idt_set_gate(16, (unsigned)isr16, 0x08, 0x8E);
    idt_set_gate(17, (unsigned)isr17, 0x08, 0x8E);
    idt_set_gate(18, (unsigned)isr18, 0x08, 0x8E);
    idt_set_gate(19, (unsigned)isr19, 0x08, 0x8E);
    idt_set_gate(20, (unsigned)isr20, 0x08, 0x8E);
    idt_set_gate(21, (unsigned)isr21, 0x08, 0x8E);
    idt_set_gate(22, (unsigned)isr22, 0x08, 0x8E);
    idt_set_gate(23, (unsigned)isr23, 0x08, 0x8E);
    idt_set_gate(24, (unsigned)isr24, 0x08, 0x8E);
    idt_set_gate(25, (unsigned)isr25, 0x08, 0x8E);
    idt_set_gate(26, (unsigned)isr26, 0x08, 0x8E);
    idt_set_gate(27, (unsigned)isr27, 0x08, 0x8E);
    idt_set_gate(28, (unsigned)isr28, 0x08, 0x8E);
    idt_set_gate(29, (unsigned)isr29, 0x08, 0x8E);
    idt_set_gate(30, (unsigned)isr30, 0x08, 0x8E);
    idt_set_gate(31, (unsigned)isr31, 0x08, 0x8E);
}

/* This is a simple string array. It contains the message that
 *  corresponds to each and every exception. We get the correct
 *  message by accessing like:
 *  exception_message[interrupt_number] */
unsigned char* exception_messages[] = {
    (unsigned char*)"DIVISION BY ZERO",
    (unsigned char*)"DEBUG",
    (unsigned char*)"NON-MASKABLE INTERRUPT",
    (unsigned char*)"BREAKPOINT",
    (unsigned char*)"DETECTED OVERFLOW",
    (unsigned char*)"OUT-OF-BOUNDS",
    (unsigned char*)"INVALID OPCODE",
    (unsigned char*)"NO COPROCESSOR",
    (unsigned char*)"DOUBLE FAULT",
    (unsigned char*)"COPROCESSOR SEGMENT OVERRUN",
    (unsigned char*)"BAD TSS",
    (unsigned char*)"SEGMENT NOT PRESENT",
    (unsigned char*)"STACK FAULT",
    (unsigned char*)"GENERAL PROTECTION FAULT",
    (unsigned char*)"PAGE FAULT",
    (unsigned char*)"UNKNOWN INTERRUPT",
    (unsigned char*)"COPROCESSOR FAULT",
    (unsigned char*)"ALIGNMENT CHECK",
    (unsigned char*)"MACHINE CHECK",
    (unsigned char*)"RESERVED",
    (unsigned char*)"RESERVED",
    (unsigned char*)"RESERVED",
    (unsigned char*)"RESERVED",
    (unsigned char*)"RESERVED",
    (unsigned char*)"RESERVED",
    (unsigned char*)"RESERVED",
    (unsigned char*)"RESERVED",
    (unsigned char*)"RESERVED",
    (unsigned char*)"RESERVED",
    (unsigned char*)"RESERVED",
    (unsigned char*)"RESERVED",
    (unsigned char*)"RESERVED"
};

/* All of our Exception handling Interrupt Service Routines will
 *  point to this function. This will tell us what exception has
 *  happened! Right now, we simply halt the system by hitting an
 *  endless loop. All ISRs disable interrupts while they are being
 *  serviced as a 'locking' mechanism to prevent an IRQ from
 *  happening and messing up kernel data structures */
void fault_handler(struct regs* r)
{
    /* Is this a fault whose number is from 0 to 31? */
    if (r->int_no < 32) {
        /* Display the description for the Exception that occurred.
         *  In this tutorial, we will simply halt the system using an
         *  infinite loop */
        terminal_write(exception_messages[r->int_no], strlen(exception_messages[r->int_no]));
        terminal_write(" Exception. System Halted!\n", sizeof(" Exception. System Halted!\n"));
        for (;;)
            ;
    }
}
