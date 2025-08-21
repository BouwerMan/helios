#include <drivers/tty.h>
#include <kernel/irq_log.h>
#include <kernel/work_queue.h>

#define IRQ_LOG_SIZE 16384 // 16 KiB, should be a power of 2

static char irq_log_buffer[IRQ_LOG_SIZE];
static volatile size_t irq_log_head = 0;
static volatile size_t irq_log_tail = 0;

// A spinlock for the *reader* side, to prevent races when dumping the log.
static spinlock_t irq_log_read_lock;

extern struct vfs_file* g_kernel_console;

void irq_log_init()
{
	spin_init(&irq_log_read_lock);
}

/**
 * Flushes the irq log. Only used in fault scenarios
 * Caller should call console_flush afterwards
 */
void irq_log_flush()
{
	irq_log_read_lock = 0;
	irq_log_drain(nullptr);
}

ssize_t irq_log_write(const char* str, size_t len)
{
	size_t i = 0;

	for (; i < len; i++) {
		irq_log_buffer[irq_log_head] = str[i];
		// Fast modulo
		irq_log_head = (irq_log_head + 1) & (IRQ_LOG_SIZE - 1);

		if (irq_log_head == irq_log_tail) {
			irq_log_tail = (irq_log_tail + 1) & (IRQ_LOG_SIZE - 1);
		}
	}

	add_work_item(irq_log_drain, nullptr);

	return (ssize_t)i;
}

/**
 * irq_log_drain - Drains irq log buffer
 */
void irq_log_drain(void* data)
{
	(void)data;

	spin_lock(&irq_log_read_lock);

	if (irq_log_head == irq_log_tail) {
		spin_unlock(&irq_log_read_lock);
		return;
	}

	if (irq_log_head > irq_log_tail) {
		// Case 1: No wrap-around. The data is in a single contiguous block.
		// [ ... tail ...... head ... ]

		vfs_file_write(g_kernel_console,
			       &irq_log_buffer[irq_log_tail],
			       irq_log_head - irq_log_tail);
	} else {
		// Case 2: Wrap-around. The data is in two parts.
		// [ ... head ...... tail ... ]

		// Part 1: from tail to the end of the buffer.
		vfs_file_write(g_kernel_console,
			       &irq_log_buffer[irq_log_tail],
			       IRQ_LOG_SIZE - irq_log_tail);
		// Part 2: from the start of the buffer to the head.
		vfs_file_write(g_kernel_console, irq_log_buffer, irq_log_head);
	}

	// Atomically update the tail to the new position.
	irq_log_tail = irq_log_head;

	spin_unlock(&irq_log_read_lock);
}
