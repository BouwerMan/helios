/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <drivers/ata/controller.h>

enum {
	OP_READ = 0,
	OP_WRITE = 1,
	OP_PACKET = 2,
};

enum {
	COMMAND_IDENTIFY = 0xEC,
	COMMAND_IDENTIFY_PACKET = 0xA1,
	COMMAND_READ_SEC = 0x20,
	COMMAND_READ_SEC_EXT = 0x24,
	COMMAND_WRITE_SEC = 0x30,
	COMMAND_WRITE_SEC_EXT = 0x34,
	COMMAND_READ_DMA = 0xC8,
	COMMAND_READ_DMA_EXT = 0x25,
	COMMAND_WRITE_DMA = 0xCA,
	COMMAND_WRITE_DMA_EXT = 0x35,
	COMMAND_PACKET = 0xA0,
	COMMAND_ATAPI_RESET = 0x8,
	COMMAND_CACHE_FLUSH = 0xE7,
};

enum {
	SCSI_CMD_READ_SECTORS = 0x28,
	SCSI_CMD_READ_SECTORS_EXT = 0xA8,
	SCSI_CMD_READ_CAPACITY = 0x25,
};

/* io-ports, offsets from base */
enum {
	ATA_REG_DATA = 0x0,
	ATA_REG_ERROR = 0x1,
	ATA_REG_FEATURES = 0x1,
	ATA_REG_SECTOR_COUNT = 0x2,
	ATA_REG_ADDRESS1 = 0x3,
	ATA_REG_ADDRESS2 = 0x4,
	ATA_REG_ADDRESS3 = 0x5,
	ATA_REG_DRIVE_SELECT = 0x6,
	ATA_REG_COMMAND = 0x7,
	ATA_REG_STATUS = 0x7,
	ATA_REG_CONTROL = 0x206,
};

enum {
	/* Drive is preparing to accept/send data -- wait until this bit clears. If it never
     * clears, do a Software Reset. Technically, when BSY is set, the other bits in the
     * Status byte are meaningless. */
	CMD_ST_BUSY = 1 << 7, /* 0x80 */
	/* Bit is clear when device is spun down, or after an error. Set otherwise. */
	CMD_ST_READY = 1 << 6, /* 0x40 */
	/* Drive Fault Error (does not set ERR!) */
	CMD_ST_DISK_FAULT = 1 << 5, /* 0x20 */
	/* Overlapped Mode Service Request */
	CMD_ST_OVERLAPPED_REQ = 1 << 4, /* 0x10 */
	/* Set when the device has PIO data to transfer, or is ready to accept PIO data. */
	CMD_ST_DRQ = 1 << 3, /* 0x08 */
	/* Error flag (when set). Send a new command to clear it (or nuke it with a Software Reset). */
	CMD_ST_ERROR = 1 << 0, /* 0x01 */
};

enum {
	/* Set this to read back the High Order Byte of the last LBA48 value sent to an IO port. */
	CTRL_HIGH_ORDER_BYTE = 1 << 7, /* 0x80 */
	/* Software Reset -- set this to reset all ATA drives on a bus, if one is misbehaving. */
	CTRL_SOFTWARE_RESET = 1 << 2, /* 0x04 */
	/* Set this to stop the current device from sending interrupts. */
	CTRL_NIEN = 1 << 1, /* 0x02 */
};

enum DEVICE_INFO_DATA {
	ATA_INFO_CAPABILITY = 49,
	ATA_INFO_SECTORS_LOW = 60,
	ATA_INFO_SECTORS_HIGH = 61,
};

#define DMA_TRANSFER_TIMEOUT   3000 /* ms */
#define DMA_TRANSFER_SLEEPTIME 20   /* ms */

#define PIO_TRANSFER_TIMEOUT   3000 /* ms */
#define PIO_TRANSFER_SLEEPTIME 0    /* ms */

#define ATAPI_TRANSFER_TIMEOUT	 3000 /* ms */
#define ATAPI_TRANSFER_SLEEPTIME 20   /* ms */

#define ATAPI_WAIT_TIMEOUT 5000 /* ms */
#define ATA_WAIT_TIMEOUT   500	/* ms */
#define ATA_WAIT_SLEEPTIME 20	/* ms */

#define IRQ_POLL_INTERVAL 20   /* ms */
#define IRQ_TIMEOUT	  5000 /* ms */

/* port-bases */
#define ATA_REG_BASE_PRIMARY   0x1F0
#define ATA_REG_BASE_SECONDARY 0x170

#define DRIVE_MASTER 0xA0
#define DRIVE_SLAVE  0xB0

#define SLAVE_BIT 0x1

#define ATAPI_SEC_SIZE 2048
#define ATA_SEC_SIZE   512

/* the LBA-flag for the device-register */
#define DEVICE_LBA  0x40
#define LBA_SUPPORT (1 << 9)

void device_init(sATADevice* device);
bool device_poll(sATADevice* device);
