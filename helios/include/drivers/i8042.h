/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "kernel/compiler_attributes.h" /* enforces little-endian bitfield packing at compile time */
#include "kernel/types.h"

/**
 * @addtogroup drivers
 * @{
 */

static constexpr size_t I8042_IO_MAX_TRIES = 1024;

/**
 * Bound for polling a device's response to a reset command. Device
 * self-test (BAT) can take real wall-clock time (up to ~750 ms per spec),
 * unlike a normal controller round trip, so this is paced with io_wait()
 * between tries rather than busy-spun like I8042_IO_MAX_TRIES. The value is
 * a conservative estimate, not a calibrated time bound.
 */
static constexpr size_t I8042_RESET_MAX_TRIES = 200000;

enum I8042_PORTS {
	I8042_DATA = 0x60,   /**< i8042 data port. Read/Write. */
	I8042_STATUS = 0x64, /**< i8042 status port. Read only. */
	I8042_CMD = 0x64,    /**< i8042 command port. Write only. */
};

enum I8042_COMMANDS {
	I8042_CMD_READ_CONFIG = 0x20,	     /**< Read byte 0 of the controller RAM. The controller sends back the
					       configuration byte. Add 1 to 0x1F to this command to read a different RAM
					       byte (0x21 to 0x3F); only byte 0 has a defined meaning. */
	I8042_CMD_WRITE_CONFIG = 0x60,	     /**< Write byte 0 of the controller RAM. Send the configuration byte next.
					       Add 1 to 0x1F to this command to write a different RAM byte (0x61 to
					       0x7F); only byte 0 has a defined meaning. */
	I8042_CMD_DISABLE_PORT2 = 0xA7,	     /**< Turn off the second PS/2 port. Only works if the controller supports
						two ports. */
	I8042_CMD_ENABLE_PORT2 = 0xA8,	     /**< Turn on the second PS/2 port. Only works if the controller supports
					       two ports. */
	I8042_CMD_TEST_PORT2 = 0xA9,	     /**< Test the second PS/2 port. The controller sends back a result byte.
					       See @ref I8042_PORT_TEST_RESULT. */
	I8042_CMD_TEST_CONTROLLER = 0xAA,    /**< Test the controller itself. The controller sends back a result byte.
						See @ref I8042_SELF_TEST_RESULT. */
	I8042_CMD_TEST_PORT1 = 0xAB,	     /**< Test the first PS/2 port. The controller sends back a result byte.
					       See @ref I8042_PORT_TEST_RESULT. */
	I8042_CMD_DIAGNOSTIC_DUMP = 0xAC,    /**< Read all bytes of the controller RAM. The response has no defined
						standard format. */
	I8042_CMD_DISABLE_PORT1 = 0xAD,	     /**< Turn off the first PS/2 port. */
	I8042_CMD_ENABLE_PORT1 = 0xAE,	     /**< Turn on the first PS/2 port. */
	I8042_CMD_READ_INPUT_PORT = 0xC0,    /**< Read the controller input port. The response bits have no defined
						standard meaning. */
	I8042_CMD_COPY_INPUT_LOW = 0xC1,     /**< Copy bits 0 to 3 of the input port to status bits 4 to 7. */
	I8042_CMD_COPY_INPUT_HIGH = 0xC2,    /**< Copy bits 4 to 7 of the input port to status bits 4 to 7. */
	I8042_CMD_READ_OUTPUT_PORT = 0xD0,   /**< Read the controller output port. */
	I8042_CMD_WRITE_OUTPUT_PORT = 0xD1,  /**< Write the controller output port. Send the new byte next. Make
						sure the output buffer is empty first. */
	I8042_CMD_WRITE_PORT1_OUTPUT = 0xD2, /**< Write the first PS/2 port output buffer. Send the byte next.
						This makes the byte look like it came from the first port. */
	I8042_CMD_WRITE_PORT2_OUTPUT = 0xD3, /**< Write the second PS/2 port output buffer. Send the byte next.
						This makes the byte look like it came from the second port. */
	I8042_CMD_WRITE_PORT2_INPUT = 0xD4,  /**< Send the next byte to the second PS/2 port. */
	I8042_CMD_PULSE_OUTPUT_LINES = 0xF0, /**< Pulse an output line low for 6 ms. Bits 0 to 3 of this command
						pick the lines: 0 pulses the line, 1 leaves it alone. Bit 0 is the
						reset line; the other lines have no defined standard purpose. Values
						0xF0 to 0xFF select different line masks. */
};

enum I8042_PORT_TEST_RESULT {
	I8042_PORT_TEST_PASSED = 0x00,		 /**< The port passed the test. */
	I8042_PORT_TEST_CLOCK_STUCK_LOW = 0x01,	 /**< The port failed the test. The clock line is stuck low. */
	I8042_PORT_TEST_CLOCK_STUCK_HIGH = 0x02, /**< The port failed the test. The clock line is stuck high. */
	I8042_PORT_TEST_DATA_STUCK_LOW = 0x03,	 /**< The port failed the test. The data line is stuck low. */
	I8042_PORT_TEST_DATA_STUCK_HIGH = 0x04,	 /**< The port failed the test. The data line is stuck high. */
};

enum I8042_SELF_TEST_RESULT {
	I8042_SELF_TEST_PASSED = 0x55, /**< The controller passed its self-test. */
	I8042_SELF_TEST_FAILED = 0xFC, /**< The controller failed its self-test. */
};

enum I8042_DEVICE_RESET_RESULT {
	I8042_DEVICE_RESET_ACK = 0xFA,	  /**< The device acknowledged the reset command. */
	I8042_DEVICE_RESET_PASSED = 0xAA, /**< The device passed its self-test after reset. */
	I8042_DEVICE_RESET_FAILED = 0xFC, /**< The device failed its self-test after reset. */
};

typedef union {
	struct {
		u8 obf	    : 1; /**< Bit 0: set when the controller has a byte ready to read. */
		u8 ibf	    : 1; /**< Bit 1: set when the controller has not yet read a byte you sent. */
		u8 sys	    : 1; /**< Bit 2: set by firmware after the controller passes its power-on test. */
		u8 cmd_data : 1; /**< Bit 3: set when the last byte sent was a command, not data. */
		u8 kbd_lock : 1; /**< Bit 4: chipset-specific. Some controllers set this for the keyboard lock switch.
				  */
		u8 aux_obf : 1;	 /**< Bit 5: chipset-specific. Many controllers set this for a byte from the second PS/2
				    port. */
		u8 timeout : 1;	 /**< Bit 6: set when the controller does not get a reply in time. */
		u8 parity  : 1;	 /**< Bit 7: set when the controller finds a parity error in a byte it read. */
	};
	u8 raw;
} i8042_status_t;

typedef union {
	struct {
		u8 int1 : 1;  /**< First PS/2 port interrupt (1 = enabled, 0 = disabled). */
		u8 int2 : 1;  /**< Second PS/2 port interrupt (1 = enabled, 0 = disabled, only if 2 PS/2 ports
				 supported). */
		u8 sys	 : 1; /**< System Flag (1 = system passed POST, 0 = your OS shouldn't be running). */
		u8 zero1 : 1; /**< Should be zero. */
		u8 clk1	 : 1; /**< First PS/2 port clock (1 = disabled, 0 = enabled). */
		u8 clk2 : 1; /**< Second PS/2 port clock (1 = disabled, 0 = enabled, only if 2 PS/2 ports supported). */
		u8 trans1 : 1; /**< First PS/2 port translation (1 = enabled, 0 = disabled). */
		u8 zero2  : 1; /**< Must be zero. */
	};
	u8 raw;
} i8042_configuration_t;

[[nodiscard]]
int i8042_init();

int i8042_write_cmd(u8 cmd);
int i8042_write_data(u8 data);
int i8042_read_data(u8* out);
int i8042_write_aux(u8 data); /* 0xD4 prefix */
bool i8042_has_aux(void);

/** @} */
