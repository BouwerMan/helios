/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <drivers/tty.h>
#include <kernel/types.h>
#include <stddef.h>

static constexpr u16 COM1_PORT = 0x3f8;

/**
 * @brief Initializes the serial port for communication.
 *
 * @return 0 if the serial port is initialized successfully, -1 if the loopback
 *         test fails.
 */
int serial_port_init();

/**
 * serial_tty_init - Initialize the serial port TTY device
 */
void serial_tty_init();

/**
 * @brief Writes a character to the serial port.
 * @param a The character to write to the serial port.
 */
void write_serial(char a);

/**
 * @brief Writes a null-terminated string to the serial port.
 * @param s The null-terminated string to write to the serial port.
 */
void write_serial_string(const char* s);

/**
 * serial_tty_write - Drain the TTY output buffer to the serial port
 * @tty: Pointer to the TTY device whose output buffer to drain
 */
ssize_t serial_tty_write(struct tty* tty);

void write_serial_n(const char* s, size_t len);
