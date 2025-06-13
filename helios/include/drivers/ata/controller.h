/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <arch/ports.h>
#include <drivers/ata/partition.h>
#include <drivers/pci/pci.h>

enum {
	DEVICE_PRIMARY = 0,
	DEVICE_SECONDARY = 1,
};

/* device-identifier */
enum {
	DEVICE_PRIM_MASTER = 0,
	DEVICE_PRIM_SLAVE = 1,
	DEVICE_SEC_MASTER = 2,
	DEVICE_SEC_SLAVE = 3,
};

enum {
	BMR_REG_COMMAND = 0x0,
	BMR_REG_STATUS = 0x2,
	BMR_REG_PRDT = 0x4,
};

enum {
	BMR_STATUS_IRQ = 0x4,
	BMR_STATUS_ERROR = 0x2,
	BMR_STATUS_DMA = 0x1,
};

enum {
	BMR_CMD_START = 0x1,
	BMR_CMD_READ = 0x8,
};

#define IO_PORTBASE_PRIMARY   ((uint16_t)0x3F6)
#define IO_PORTBASE_SECONDARY ((uint16_t)0x376)
#define PRDT_EOT	      (1 << 15)
#define CTRL_IRQ_BASE	      14

typedef struct sATAController sATAController;
typedef struct sATADevice sATADevice;
typedef bool (*fReadWrite)(sATADevice* device, uint16_t op, void* buffer, uint32_t lba, size_t secSize,
			   size_t secCount);

struct sATADevice {
	/* the identifier; 0-3; bit0 set means slave */
	uint8_t id;
	/* whether the device exists and we can use it */
	uint8_t present;
	/* master / slave */
	uint8_t slave_bit;
	/* the sector-size */
	size_t sec_size;
	/* the ata-controller to which the device belongs */
	sATAController* ctrl;
	/* handler-function for reading / writing */
	fReadWrite rw_handler;
	/* various informations we got via IDENTIFY-command */
	uint16_t info[256];
	// sATAIdentify info;
	/* the partition-table */
	sPartition part_table[PARTITION_COUNT];
};

struct sATAController {
	uint8_t id;
	uint8_t use_irq;
	uint8_t use_dma;
	/* I/O-ports for the controllers */
	uint16_t port_base;
	uint16_t IO_port_base;
	/* I/O-ports for bus-mastering */
	uint16_t bmr_base;
	int irq;
	int irqsem;
	sATADevice devices[2];
	struct PRDT* prdt;
	const pci_device_t* ide_ctrl;
};

struct PRDT {
	uint32_t addr;	// Address of memory buffer
	uint16_t size;	// Size of memory buffer
	uint16_t flags; // The last entry in the table must have the EOT
			// (End-of-Table) flag set.
} __attribute__((packed));

static inline void ctrl_bmr_outb(sATAController* ctrl, uint16_t reg, uint8_t value)
{
	outb(ctrl->bmr_base + reg, value);
}

static inline void ctrl_bmr_outd(sATAController* ctrl, uint16_t reg, uint32_t value)
{
	outdword(ctrl->bmr_base + reg, value);
}

static inline uint8_t ctrl_bmr_inb(sATAController* ctrl, uint16_t reg)
{
	return inb(ctrl->bmr_base + reg);
}

static inline uint32_t ctrl_bmr_ind(sATAController* ctrl, uint16_t reg)
{
	return indword(ctrl->bmr_base + reg);
}

void ctrl_init();

sATADevice* ctrl_get_device(uint8_t id);

void ctrl_outb(sATAController* ctrl, uint16_t reg, uint8_t value);

uint8_t ctrl_inb(sATAController* ctrl, uint16_t reg);
uint16_t ctrl_inw(sATAController* ctrl, uint16_t reg);

// in words
void ctrl_inws(sATAController* ctrl, uint16_t reg, uint16_t* buff, size_t count);

// out words
void ctrl_outws(sATAController* ctrl, uint16_t reg, const uint16_t* buff, size_t count);

/**
 * Performs a few io-port-reads (just to waste a bit of time ;))
 *
 * @param ctrl the controller
 */
void ctrl_wait(sATAController* ctrl);
