/* SPDX-License-Identifier: GPL-3.0-or-later */
// Big thanks to:
// https://github.com/Andrispowq/HackOS/blob/master/kernel/src/arch/x86_64/interrupts/idt.h
#pragma once
#include <stdint.h>

#include <arch/regs.h>
#include <kernel/types.h>

static constexpr int KERNEL_CS	 = 0x08;
static constexpr int IDT_ENTRIES = 256;

static constexpr u16 PIC1_COMMAND = 0x20;
static constexpr u16 PIC1_DATA	  = 0x21;
static constexpr u16 PIC2_COMMAND = 0xA0;
static constexpr u16 PIC2_DATA	  = 0xA1;
static constexpr u16 PIC_EOI	  = 0x20;

static constexpr u16 ICW1_INIT = 0x10;
static constexpr u16 ICW1_ICW4 = 0x01;
static constexpr u16 ICW4_8086 = 0x01;

enum IRQn {
	IRQ0  = 32,
	IRQ1  = 33,
	IRQ2  = 34,
	IRQ3  = 35,
	IRQ4  = 36,
	IRQ5  = 37,
	IRQ6  = 38,
	IRQ7  = 39,
	IRQ8  = 40,
	IRQ9  = 41,
	IRQ10 = 42,
	IRQ11 = 43,
	IRQ12 = 44,
	IRQ13 = 45,
	IRQ14 = 46,
	IRQ15 = 47,
};

typedef struct {
	uint16_t isr_low;   // The lower 16 bits of the ISR's address
	uint16_t kernel_cs; // The GDT segment selector that the CPU will load into CS before calling the ISR
	uint8_t ist;	    // The IST in the TSS that the CPU will load into RSP; set to zero for now
	uint8_t attributes; // Type and attributes; see the IDT page
	uint16_t isr_mid;   // The higher 16 bits of the lower 32 bits of the ISR's address
	uint32_t isr_high;  // The higher 32 bits of the ISR's address
	uint32_t reserved;  // Set to zero
} __attribute__((packed)) idt_entry_t;

typedef struct idtr {
	uint16_t limit;
	uint64_t base;
} __attribute__((packed)) idtr_t;

struct xmm_reg {
	uint64_t low;
	uint64_t high;
};

// IDT
void idt_set_descriptor(uint8_t vector, uint64_t isr, uint8_t flags);
void idt_init(void);

// ISR
void isr_init();
void install_isr_handler(int isr, void (*handler)(struct registers* r));
void uninstall_isr_handler(int isr);
void isr_handler(struct registers* r);

// IRQ
void irq_init(void);
void irq_handler(struct registers* r);

void IRQ_set_mask(uint8_t IRQline);
void IRQ_clear_mask(uint8_t IRQline);

extern void __set_idt(idtr_t* idtr);
/* ISR definitions */
extern void isr0();
extern void isr1();
extern void isr2();
extern void isr3();
extern void isr4();
extern void isr5();
extern void isr6();
extern void isr7();
extern void isr8();
extern void isr9();
extern void isr10();
extern void isr11();
extern void isr12();
extern void isr13();
extern void isr14();
extern void isr15();
extern void isr16();
extern void isr17();
extern void isr18();
extern void isr19();
extern void isr20();
extern void isr21();
extern void isr22();
extern void isr23();
extern void isr24();
extern void isr25();
extern void isr26();
extern void isr27();
extern void isr28();
extern void isr29();
extern void isr30();
extern void isr31();
extern void isr48(); //yield

/* IRQ definitions */
extern void irq0();
extern void irq1();
extern void irq2();
extern void irq3();
extern void irq4();
extern void irq5();
extern void irq6();
extern void irq7();
extern void irq8();
extern void irq9();
extern void irq10();
extern void irq11();
extern void irq12();
extern void irq13();
extern void irq14();
extern void irq15();

/* syscall handler */
extern void isr128();
