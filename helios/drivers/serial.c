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

/**
 * @brief Initializes the serial port for communication.
 *
 * This function configures the serial port by setting the baud rate, enabling
 * interrupts, and configuring the data format. It also performs a loopback
 * test to verify the functionality of the serial port. If the test fails,
 * the function returns an error code.
 *
 * @return 0 if the serial port is initialized successfully, 1 if the loopback
 *         test fails.
 */
int init_serial(void)
{
	outb(COM1_PORT + 1, 0x00); // Disable all interrupts
	outb(COM1_PORT + 3, 0x80); // Enable DLAB (set baud rate divisor)
	outb(COM1_PORT + 0, 0x03); // Set divisor to 3 (lo byte) 38400 baud
	outb(COM1_PORT + 1, 0x00); //                  (hi byte)
	outb(COM1_PORT + 3, 0x03); // 8 bits, no parity, one stop bit
	outb(COM1_PORT + 2, 0xC7); // Enable FIFO, clear them, with 14-byte threshold
	outb(COM1_PORT + 4, 0x0B); // IRQs enabled, RTS/DSR set
	outb(COM1_PORT + 4, 0x1E); // Set in loopback mode, test the serial chip
	outb(COM1_PORT + 0, 0xAE); // Test serial chip (send byte 0xAE and check if serial returns same byte)

	// Check if serial is faulty (i.e: not same byte as sent)
	if (inb(COM1_PORT + 0) != 0xAE) {
		return 1;
	}

	// If serial is not faulty set it in normal operation mode
	// (not-loopback with IRQs enabled and OUT#1 and OUT#2 bits enabled)
	outb(COM1_PORT + 4, 0x0F);
	return 0;
}

static int is_transmit_empty(void)
{
	return inb(COM1_PORT + 5) & 0x20;
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
	while (is_transmit_empty() == 0)
		;
	outb(COM1_PORT, (uint8_t)a);
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
	while (*s)
		write_serial(*s++);
}
