/**
 * @file drivers/serial.c
 *
 * Copyright (C) 2025  Dylan Parks
 *
 * This file is part of HeliOS
 *
 * HeliOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <arch/ports.h>
#include <drivers/serial.h>
#include <drivers/tty.h>
#include <kernel/panic.h>
#include <mm/kmalloc.h>
#include <mm/page.h>
#include <mm/page_alloc.h>

/*******************************************************************************
 * Global Variable Definitions
 *******************************************************************************/

struct tty_driver serial_driver = {
	.write = serial_tty_write,
};

static constexpr size_t RING_BUFFER_SIZE_PAGES = 8;
static constexpr size_t RING_BUFFER_SIZE = RING_BUFFER_SIZE_PAGES * PAGE_SIZE;

/*******************************************************************************
 * Private Function Prototypes
 *******************************************************************************/

static inline int is_transmit_empty(void)
{
	return inb(COM1_PORT + 5) & 0x20;
}

/*******************************************************************************
 * Public Function Definitions
 *******************************************************************************/

/**
 * @brief Initializes the serial port for communication.
 *
 * This function configures the serial port by setting the baud rate, enabling
 * interrupts, and configuring the data format. It also performs a loopback
 * test to verify the functionality of the serial port. If the test fails,
 * the function returns an error code.
 *
 * @return 0 if the serial port is initialized successfully, -1 if the loopback
 *         test fails.
 */
int serial_port_init()
{
	outb(COM1_PORT + 1, 0x00); // Disable all interrupts
	outb(COM1_PORT + 3, 0x80); // Enable DLAB (set baud rate divisor)
	outb(COM1_PORT + 0, 0x03); // Set divisor to 3 (lo byte) 38400 baud
	outb(COM1_PORT + 1, 0x00); //                  (hi byte)
	outb(COM1_PORT + 3, 0x03); // 8 bits, no parity, one stop bit
	outb(COM1_PORT + 2,
	     0xC7); // Enable FIFO, clear them, with 14-byte threshold
	outb(COM1_PORT + 4, 0x0B); // IRQs enabled, RTS/DSR set
	outb(COM1_PORT + 4, 0x1E); // Set in loopback mode, test the serial chip
	outb(COM1_PORT + 0,
	     0xAE); // Test serial chip (send byte 0xAE and check if serial returns same byte)

	// Check if serial is faulty (i.e: not same byte as sent)
	if (inb(COM1_PORT + 0) != 0xAE) {
		return -1;
	}

	// If serial is not faulty set it in normal operation mode
	// (not-loopback with IRQs enabled and OUT#1 and OUT#2 bits enabled)
	outb(COM1_PORT + 4, 0x0F);
	return 0;
}

/**
 * serial_tty_init - Initialize the serial port TTY device
 *
 * Creates and registers a TTY device named "ttyS0" that outputs to the
 * serial port. Allocates memory for the output ring buffer and initializes
 * all necessary data structures. This TTY provides serial console access
 * for debugging and remote system administration.
 *
 * Panics if memory allocation for the ring buffer fails, as the serial
 * TTY is essential for system debugging and logging.
 */
void serial_tty_init()
{
	struct tty* tty = kzalloc(sizeof(struct tty));
	tty->driver = &serial_driver;
	strncpy(tty->name, "ttyS0", 32);

	struct ring_buffer* rb = &tty->output_buffer;
	rb->buffer = get_free_pages(AF_KERNEL, RING_BUFFER_SIZE_PAGES);
	if (!rb->buffer) {
		panic("Didn't get free pages");
	}
	rb->size = RING_BUFFER_SIZE;
	spin_init(&rb->lock);

	sem_init(&tty->write_lock, 1);
	register_tty(tty);
}

[[gnu::always_inline, gnu::used]]
static inline void __write_char(char a)
{
	while (is_transmit_empty() == 0) {
		__builtin_ia32_pause();
	}
	outb(COM1_PORT, (u8)a);
}

[[gnu::always_inline, gnu::used]]
static inline void __write_debugcon(char a)
{
	outb(0xE9, (u8)a);
}

/**
 * @brief Writes a character to the serial port.
 *
 * This function waits until the serial port is ready to transmit data,
 * then sends the specified character.
 *
 * @param a The character to write to the serial port.
 */
void write_serial(char a)
{
#ifdef DEBUGCON
	__write_debugcon(a);
#else
	__write_char(a);
#endif /* DEBUGCON */
}

/**
 * @brief Writes a null-terminated string to the serial port.
 *
 * This function iterates through each character in the provided string
 * and writes it to the serial port using `write_serial`.
 *
 * @param s The null-terminated string to write to the serial port.
 */
void write_serial_string(const char* s)
{
	while (*s) {
#ifdef DEBUGCON
		__write_debugcon(*s++);
#else
		__write_char(*s++);
#endif /* DEBUGCON */
	}
}

void write_serial_n(const char* s, size_t len)
{
	if (!s || len == 0) return;

	const char* end = s + len;
	while (s < end) {
#ifdef DEBUGCON
		__write_debugcon(*s++);
#else
		__write_char(*s++);
#endif /* DEBUGCON */
	}
}

/**
 * serial_tty_write - Drain the TTY output buffer to the serial port
 * @tty: Pointer to the TTY device whose output buffer to drain
 *
 * Reads all available characters from the TTY's output ring buffer and
 * transmits them through the serial port. This function is typically called
 * as a work item to process buffered output. The operation is atomic and
 * protected by the ring buffer's spinlock to ensure thread safety.
 *
 * Return: Number of characters written to the serial port
 */
ssize_t serial_tty_write(struct tty* tty)
{
	struct ring_buffer* rb = &tty->output_buffer;
	ssize_t bytes_written = 0;

	sem_wait(&tty->write_lock);

	while (rb->head != rb->tail) {
		write_serial(rb->buffer[rb->tail]);
		rb->tail = (rb->tail + 1) % rb->size;
		bytes_written++;
	}

	sem_signal(&tty->write_lock);

	return bytes_written;
}
