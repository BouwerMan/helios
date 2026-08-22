/**
 * @file drivers/i8042.c
 *
 * Copyright (C) 2026 Dylan Parks
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

#include "drivers/i8042.h"
#include "arch/ports.h"
#include "kernel/rwonce.h"
#include "lib/log.h"

#include <uapi/helios/errno.h>

/**
 * @addtogroup drivers
 * @{
 */

static bool has_aux = false;

/**
 * @brief Set up the i8042 controller.
 *
 * This function makes the i8042 controller ready for use. The driver must
 * call this function before it sends a command to the controller.
 *
 * @return 0 if success or -ENODEV if initialization failed
 */
int i8042_init()
{
	// Start with ports disabled
	if (i8042_write_cmd(I8042_CMD_DISABLE_PORT1) < 0) {
		log_error("Failed to disable i8042 port 1");
		return -ENODEV;
	}

	if (i8042_write_cmd(I8042_CMD_DISABLE_PORT2) < 0) {
		log_error("Failed to disable i8042 port 2");
		return -ENODEV;
	}

	// Flush output buffer. An empty buffer is the normal case, so drain
	// while a byte is waiting instead of waiting for one to appear.
	for (size_t i = 0; i < I8042_IO_MAX_TRIES; i++) {
		i8042_status_t st;
		st.raw = inb(I8042_STATUS);
		if (!st.obf) break;
		inb(I8042_DATA);
	}

	i8042_configuration_t cfg;
	if (i8042_write_cmd(I8042_CMD_READ_CONFIG) < 0 || i8042_read_data(&cfg.raw) < 0) {
		log_error("Failed to read i8042 configuration byte");
		return -ENODEV;
	}

	// Disable IRQs for port 1
	cfg.int1 = 0;
	cfg.trans1 = 0;
	cfg.clk1 = 0; // Enable port 1 clock

	if (i8042_write_cmd(I8042_CMD_WRITE_CONFIG) < 0 || i8042_write_data(cfg.raw) < 0) {
		log_error("Failed to write back i8042 configuration byte");
		return -ENODEV;
	}

	// Self-test the controller
	u8 test_result = 0;
	if (i8042_write_cmd(I8042_CMD_TEST_CONTROLLER) < 0 || i8042_read_data(&test_result) < 0) {
		log_error("Failed to get test result from i8042 controller");
		return -ENODEV;
	}

	log_debug("i8042 controller self-test result: 0x%02x", test_result);

	if (test_result != I8042_SELF_TEST_PASSED) {
		log_error("i8042 controller failed self-test");
		return -ENODEV;
	}

	if (i8042_write_cmd(I8042_CMD_ENABLE_PORT2) < 0) {
		log_error("Failed to command i8042 port 2 to enabled");
		return -ENODEV;
	}

	cfg.raw = 0;
	if (i8042_write_cmd(I8042_CMD_READ_CONFIG) < 0 || i8042_read_data(&cfg.raw) < 0) {
		log_error("Failed to read i8042 configuration byte");
		return -ENODEV;
	}

	// Clock should be enabled if port 2 was actually enabled
	has_aux = !cfg.clk2;
	if (has_aux) {
		log_debug("i8042 supports 2 channels");

		if (i8042_write_cmd(I8042_CMD_DISABLE_PORT2) < 0) {
			log_error("Failed to disable i8042 port 2");
			return -ENODEV;
		}

		// Disable IRQs for port 2
		cfg.int2 = 0;
		cfg.clk2 = 0; // Enable port 2 clock

		if (i8042_write_cmd(I8042_CMD_WRITE_CONFIG) < 0 || i8042_write_data(cfg.raw) < 0) {
			log_error("Failed to write back i8042 configuration byte");
			return -ENODEV;
		}
	} else {
		log_debug("i8042 does not support 2 channels");
	}

	// Now onto port self-tests
	bool port1_works = false;
	bool port2_works = false;
	test_result = 0;
	if (i8042_write_cmd(I8042_CMD_TEST_PORT1) < 0 || i8042_read_data(&test_result) < 0) {
		log_error("Failed to run i8042 port 1 self-test");
		return -ENODEV;
	}

	log_debug("I8042 port 1 test result: 0x%02x", test_result);

	if (test_result != I8042_PORT_TEST_PASSED) {
		log_error("i8042 port 1 failed self-test");
	} else {
		port1_works = true;
	}

	if (has_aux) {
		test_result = 0;
		if (i8042_write_cmd(I8042_CMD_TEST_PORT2) < 0 || i8042_read_data(&test_result) < 0) {
			log_error("Failed to run i8042 port 2 self-test");
			return -ENODEV;
		}

		log_debug("I8042 port 2 test result: 0x%02x", test_result);

		if (test_result != I8042_PORT_TEST_PASSED) {
			log_error("i8042 port 2 failed self-test");
		} else {
			port2_works = true;
		}
	}

	// Re-enable working ports
	if (port1_works) {
		if (i8042_write_cmd(I8042_CMD_ENABLE_PORT1) < 0) {
			log_error("Failed to enable port 1");
			port1_works = false;
		}
	}
	if (port2_works) {
		if (i8042_write_cmd(I8042_CMD_ENABLE_PORT2) < 0) {
			log_error("Failed to enable port 2");
			port2_works = false;
		}
	}

	if (!port1_works && !port2_works) {
		log_error("No valid i8042 ports available");
		return -ENODEV;
	}

	// Now we re-write the configuration with IRQs enabled
	cfg.raw = 0;
	if (i8042_write_cmd(I8042_CMD_READ_CONFIG) < 0 || i8042_read_data(&cfg.raw) < 0) {
		log_error("Failed to read i8042 configuration byte");
		return -ENODEV;
	}
	cfg.int1 = port1_works;
	cfg.int2 = port2_works;
	if (i8042_write_cmd(I8042_CMD_WRITE_CONFIG) < 0 || i8042_write_data(cfg.raw) < 0) {
		log_error("Failed to write back i8042 configuration byte");
		return -ENODEV;
	}

	// Reset devices
	if (port1_works) {
		if (i8042_write_data(0xFF) < 0) {
			log_error("Failed to reset port 1 devices");
		}
	}
	if (port2_works) {
		if (i8042_write_aux(0xFF) < 0) {
			log_error("Failed to reset port 2 devices");
		}
	}

	return 0;
}

/**
 * @brief Polls the status register until it clears the ibf bit.
 *
 * @return 0 if the controller is ready to write. -ETIMEDOUT if I8042_IO_MAX_TRIES is reached.
 */
[[gnu::always_inline]]
static inline int __wait_write(void)
{
	for (size_t i = 0; i < I8042_IO_MAX_TRIES; i++) {
		i8042_status_t st;
		st.raw = inb(I8042_STATUS);
		if (!st.ibf) return 0;
	}
	return -ETIMEDOUT;
}

/**
 * @brief Polls the status register until it sets the obf bit.
 *
 * @return 0 if the controller is ready to read. -ETIMEDOUT if I8042_IO_MAX_TRIES is reached.
 */
[[gnu::always_inline]]
static inline int __wait_read(void)
{
	for (size_t i = 0; i < I8042_IO_MAX_TRIES; i++) {
		i8042_status_t st;
		st.raw = inb(I8042_STATUS);
		if (st.obf) return 0;
	}
	return -ETIMEDOUT;
}

static int i8042_write_cmd_data(u8 cmd, u8 data)
{
	int st = i8042_write_cmd(cmd);
	if (st < 0) return st;
	return i8042_write_data(data);
}

/**
 * @brief Send a command byte to the i8042 controller.
 *
 * This function writes one byte to the command port. The controller reads
 * the byte as a command, not as data.
 *
 * @param cmd The command byte to send.
 * @return 0 if the controller accepts the byte. A negative value if the write fails.
 */
int i8042_write_cmd(u8 cmd)
{
	int st = __wait_write();
	if (st < 0) return st;
	outb(I8042_CMD, cmd);
	return 0;
}

/**
 * @brief Send a data byte to the i8042 controller.
 *
 * This function writes one byte to the data port. Use this function to send
 * data to the keyboard or to the controller.
 *
 * @param data The data byte to send.
 * @return 0 if the controller accepts the byte. A negative value if the write fails.
 */
int i8042_write_data(u8 data)
{
	int st = __wait_write();
	if (st < 0) return st;
	outb(I8042_DATA, data);
	return 0;
}

/**
 * @brief Read a data byte from the i8042 controller.
 *
 * This function reads one byte from the data port. The function must wait
 * until the controller has a byte ready before it reads the port.
 *
 * @param out A pointer to a byte. The function stores the byte it reads at this address. Nullptr discards result.
 * @return 0 if the function reads a byte. A negative value if the read fails.
 */
int i8042_read_data(u8* out)
{
	int st = __wait_read();
	if (st < 0) return st;
	u8 res = inb(I8042_DATA);
	if (out) {
		*out = res;
	}
	return 0;
}

/**
 * @brief Send a data byte to the second PS/2 port.
 *
 * This function sends the byte 0xD4 to the command port. This byte tells the
 * controller that the next data byte is for the second PS/2 port, not the
 * first port. The function then sends the data byte to the data port.
 *
 * @param data The data byte to send to the second PS/2 port.
 * @return 0 if the controller accepts the byte. A negative value if the write fails.
 */
int i8042_write_aux(u8 data)
{
	if (!i8042_has_aux()) return -ENODEV;
	return i8042_write_cmd_data(I8042_CMD_WRITE_PORT2_INPUT, data);
}

/**
 * @brief Check if the i8042 controller has a second PS/2 port.
 *
 * This function checks if the controller supports a second PS/2 port.
 * Systems use the second port for a mouse. Not all systems have this port.
 *
 * @return true if the controller has a second PS/2 port. false if it does not.
 */
bool i8042_has_aux(void)
{
	return READ_ONCE(has_aux);
}

/** @} */
