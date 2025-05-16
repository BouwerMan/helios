#include "idt.h"
#include "../ports.h"
#include <kernel/screen.h>
#include <string.h>
#include <util/log.h>

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

/**
 * @brief Default exception handler for interrupts.
 *
 * This function is invoked when an unhandled interrupt occurs. It logs
 * detailed information about the interrupt and the state of the CPU
 * registers at the time of the exception.
 *
 * @param registers Pointer to the structure containing the CPU register
 *                  state and interrupt information.
 *
 * The function outputs:
 * - The interrupt number and error code.
 * - A description of the exception.
 * - The values of various CPU registers (RIP, RSP, RBP, general-purpose
 * registers, etc.).
 *
 * After logging the information, the function halts the CPU.
 */
static void default_exception_handler(struct registers* registers)
{
	set_log_mode(LOG_DIRECT);
	log_error("Recieved interrupt #%lx with error code %lx on the default handler!", registers->int_no,
		  registers->err_code);
	log_error("Exception: %s", exception_messages[registers->int_no]);
	log_error("RIP: %lx, RSP: %lx, RBP: %lx", registers->rip, registers->rsp, registers->rbp);
	log_error("RAX: %lx, RBX: %lx, RCX: %lx, RDX: %lx", registers->rax, registers->rbx, registers->rcx,
		  registers->rdx);
	log_error("RDI: %lx, RSI: %lx, RFLAGS: %lx, DS: %lx", registers->rdi, registers->rsi, registers->rflags,
		  registers->ds);
	log_error("CS: %lx, SS: %lx", registers->cs, registers->ss);
	log_error("R8: %lx, R9: %lx, R10: %lx, R11: %lx", registers->r8, registers->r9, registers->r10, registers->r11);
	log_error("R12: %lx, R13: %lx, R14: %lx, R15: %lx", registers->r12, registers->r13, registers->r14,
		  registers->r15);
	uint64_t fault_addr;
	__asm__ volatile("mov %%cr2, %0" : "=r"(fault_addr));
	log_error("Fault addr: %lx", fault_addr);

	__asm__ volatile("cli; hlt");
}

__attribute__((aligned(0x10))) static idt_entry_t idt[256]; // Create an array of IDT entries; aligned for performance
static idtr_t idtr;

/**
 * @brief Sets an entry in the Interrupt Descriptor Table (IDT).
 *
 * This function configures a specific IDT entry with the given interrupt
 * service routine (ISR) address, privilege level, and other attributes.
 *
 * @param vector The interrupt vector number (index in the IDT).
 * @param isr The address of the interrupt service routine.
 * @param flags The attributes for the IDT entry (e.g., privilege level, type).
 *
 * The function splits the ISR address into three parts (low, mid, high)
 * to fit into the IDT entry structure and initializes other fields such
 * as the code segment selector and reserved bits.
 */
void idt_set_descriptor(uint8_t vector, uint64_t isr, uint8_t flags)
{
	idt_entry_t* descriptor = &idt[vector];

	descriptor->isr_low = isr & 0xFFFF;
	descriptor->kernel_cs = KERNEL_CS;
	descriptor->ist = 0;
	descriptor->attributes = flags;
	descriptor->isr_mid = (isr >> 16) & 0xFFFF;
	descriptor->isr_high = (isr >> 32) & 0xFFFFFFFF;
	descriptor->reserved = 0;
}

/**
 * @brief Initializes the Interrupt Descriptor Table (IDT) and Programmable
 * Interrupt Controller (PIC).
 *
 * This function sets up the IDT, initializes the interrupt service routines
 * (ISRs), configures the PIC, and enables interrupts on the CPU.
 *
 * Steps performed:
 * 1. Sets the IDT base and limit in the IDTR register.
 * 2. Clears the IDT by initializing it to zeros.
 * 3. Initializes ISRs and IRQs.
 * 4. Loads the IDT using the `lidt` instruction.
 * 5. Configures the PIC by sending initialization commands and remapping IRQs.
 * 6. Unmasks the PIC to allow interrupts.
 * 7. Enables interrupts on the CPU using the `sti` instruction.
 */
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

/**
 * @brief Installs an interrupt service routine (ISR) handler for a specific
 * interrupt.
 *
 * This function associates a custom handler function with a specific interrupt
 * service routine (ISR). The handler will be invoked when the corresponding
 * interrupt occurs.
 *
 * @param isr The interrupt service routine number to associate the handler
 * with.
 * @param handler A pointer to the handler function to be executed when the ISR
 * is triggered. The handler function must accept a pointer to a `struct
 * registers` as its argument.
 */
void install_isr_handler(int isr, void (*handler)(struct registers* r))
{
	interrupt_handlers[isr] = (void*)handler;
}

/**
 * @brief Uninstalls an interrupt service routine (ISR) handler for a specific
 * interrupt.
 *
 * This function removes the custom handler associated with a specific interrupt
 * service routine (ISR) by setting its entry in the `interrupt_handlers` array
 * to NULL.
 *
 * @param isr The interrupt service routine number whose handler is to be
 * uninstalled.
 */
void uninstall_isr_handler(int isr)
{
	interrupt_handlers[isr] = NULL;
}

/**
 * @brief Initializes the Interrupt Descriptor Table (IDT) entries for
 *        the first 32 interrupt service routines (ISRs).
 *
 * This function sets up the IDT descriptors for the first 32 ISRs,
 * which are reserved by Intel for handling exceptions. Each descriptor
 * is configured with the appropriate ISR address and access flags.
 * Additionally, a default exception handler is installed for all ISRs.
 *
 * Access flags (0x8E):
 * - Present bit: 1 (entry is present)
 * - Descriptor privilege level: 0 (kernel level)
 * - Type: 14 (32-bit interrupt gate)
 *
 * @note This function is repetitive because each ISR must be explicitly
 *       assigned to its corresponding IDT entry. A loop cannot be used
 *       due to the need for specific function names.
 */
void isr_init()
{
	// Set IDT descriptors for the first 32 ISRs
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
	idt_set_descriptor(48, (uint64_t)isr48, 0x8E);

	// Install default exception handler for all ISRs
	for (uint8_t i = 0; i < 32; i++) {
		install_isr_handler(i, default_exception_handler);
	}
}

/**
 * @brief Handles exceptions and invokes the appropriate interrupt service
 * routine (ISR).
 *
 * This function is called whenever an exception occurs. It checks if the
 * exception number is within the range of 0 to 31. If a custom handler is
 * registered for the exception, it invokes the handler. Otherwise, it displays
 * an error message and halts the system in an infinite loop.
 *
 * @param r A pointer to the `struct registers` containing the state of the CPU
 *          at the time of the exception.
 */
void isr_handler(struct registers* r)
{
	/* Is this a fault whose number is from 0 to 31? */
	if (r->int_no < 32) {
		void (*handler)(struct registers* r);
		handler = interrupt_handlers[r->int_no];
		/* Display the description for the Exception that occurred.
     *  In this tutorial, we will simply halt the system using an
     *  infinite loop */
		if (handler) {
			screen_clear();
			handler(r);
		} else {
			log_error("%s\n%s", (char*)exception_messages[r->int_no], "Exception. System Halted!");
			for (;;)
				;
		}
	}
}

// IRQ stuff

/**
 * @brief Initializes the Interrupt Request (IRQ) handlers in the IDT.
 *
 * This function sets up the Interrupt Descriptor Table (IDT) entries for
 * hardware interrupts (IRQs) 0 through 15. Each IRQ is associated with a
 * specific interrupt service routine (ISR) defined elsewhere in the code.
 * The descriptors are configured with the appropriate segment selector
 * and attributes.
 *
 * @note The IRQs are mapped to interrupt numbers 32 through 47 in the IDT.
 *       This is because the first 32 entries (0-31) are reserved for CPU
 * exceptions.
 */
void irq_init(void)
{
	idt_set_descriptor(IRQ0, (uint64_t)irq0, 0x8E);
	idt_set_descriptor(IRQ1, (uint64_t)irq1, 0x8E);
	idt_set_descriptor(IRQ2, (uint64_t)irq2, 0x8E);
	idt_set_descriptor(IRQ3, (uint64_t)irq3, 0x8E);
	idt_set_descriptor(IRQ4, (uint64_t)irq4, 0x8E);
	idt_set_descriptor(IRQ5, (uint64_t)irq5, 0x8E);
	idt_set_descriptor(IRQ6, (uint64_t)irq6, 0x8E);
	idt_set_descriptor(IRQ7, (uint64_t)irq7, 0x8E);
	idt_set_descriptor(IRQ8, (uint64_t)irq8, 0x8E);
	idt_set_descriptor(IRQ9, (uint64_t)irq9, 0x8E);
	idt_set_descriptor(IRQ10, (uint64_t)irq10, 0x8E);
	idt_set_descriptor(IRQ11, (uint64_t)irq11, 0x8E);
	idt_set_descriptor(IRQ12, (uint64_t)irq12, 0x8E);
	idt_set_descriptor(IRQ13, (uint64_t)irq13, 0x8E);
	idt_set_descriptor(IRQ14, (uint64_t)irq14, 0x8E);
	idt_set_descriptor(IRQ15, (uint64_t)irq15, 0x8E);
}

/**
 * @brief Handles hardware interrupt requests (IRQs).
 *
 * This function is invoked for all IRQs. It checks if a custom handler is
 * registered for the specific IRQ and executes it if available. After handling
 * the IRQ, it sends an End of Interrupt (EOI) signal to the appropriate
 * interrupt controller(s) to indicate that the interrupt has been serviced.
 *
 * @param r A pointer to the `struct registers` containing the state of the CPU
 *          at the time of the interrupt.
 *
 * @note If the interrupt originates from IRQ8-IRQ15 (slave controller), an EOI
 *       must be sent to both the slave and master controllers. For IRQ0-IRQ7
 *       (master controller), only the master controller requires an EOI.
 */
void irq_handler(struct registers* r)
{
	/* This is a blank function pointer */
	void (*handler)(struct registers* r);

	/* Find out if we have a custom handler to run for this IRQ, and then finally,
   * run it */
	handler = interrupt_handlers[r->int_no];

	if (handler) {
		handler(r);
	}

	/* If the IDT entry that was invoked was greater than 40
   *  (meaning IRQ8 - 15), then we need to send an EOI to
   *  the slave controller */
	if (r->int_no >= 40) {
		outb(PIC2_COMMAND, PIC_EOI);
	}

	/* In either case, we need to send an EOI to the master
   *  interrupt controller too */
	outb(PIC1_COMMAND, PIC_EOI);
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
