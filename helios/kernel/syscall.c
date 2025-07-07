#include <arch/idt.h>
#include <kernel/dmesg.h>
#include <kernel/syscall.h>
#include <stddef.h>
#include <stdio.h>

[[gnu::always_inline]]
static inline void SYSRET(struct registers* r, u64 val)
{
	r->rax = val; // Set the return value
}

void sys_write(struct registers* r)
{
	// rdi: file descriptor, rsi: buffer, rdx: size
	if (r->rdi != 1) { // Only handle stdout for now
		return;
	}

	const char* buf = (const char*)r->rsi;
	size_t size	= r->rdx;

	for (size_t i = 0; i < size; i++) {
		dmesg_enqueue(buf, 1);
	}
	SYSRET(r, size); // Return the number of bytes written
}

typedef void (*handler)(struct registers* r);
static const handler syscall_handlers[] = {
	0,
	sys_write,
};

static constexpr int SYSCALL_COUNT = sizeof(syscall_handlers) / sizeof(syscall_handlers[0]);

/*
 * Linux-style syscalls use specific registers to pass arguments:
 * - rax: System call number
 * - rdi: First argument
 * - rsi: Second argument
 * - rdx: Third argument
 * - r10: Fourth argument
 * - r8:  Fifth argument
 * - r9:  Sixth argument
 */
void syscall_handler(struct registers* r)
{
	if (r->rax > SYSCALL_COUNT) return;
	handler func = syscall_handlers[r->rax];
	if (func) func(r);
}

void syscall_init()
{
	install_isr_handler(SYSCALL_INT, syscall_handler);
}
