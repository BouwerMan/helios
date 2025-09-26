/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <kernel/types.h>
#include <stdint.h>

enum {
	BUS_COUNT = 8,
	DEV_COUNT = 32,
	FUNC_COUNT = 8,
};

enum PCI_IOPORTS {
	IOPORT_PCI_CFG_DATA = 0xCFC,
	IOPORT_PCI_CFG_ADDR = 0xCF8,
};

enum DEV_TYPE {
	GENERIC = 0x0,
	PCI_PCI_BRIDGE = 0x1,
	CARD_BUS_BRIDGE = 0x2,
};

// TODO: These offsets are kinda wacky if I am always reading dwords
enum PCI_OFFSETS {
	PCI_VENDOR_ID = 0x00,
	PCI_DEVICE_ID = 0x02,
	PCI_COMMAND = 0x04,
	PCI_STATUS = 0x06,
	PCI_PROGRAMMING = 0x08,
	PCI_CLASS = 0x0A,
	PCI_TYPE = 0x0E,
};

static constexpr u16 VENDOR_INVALID = 0xFFFF;

static constexpr u16 BAR0 = 0x10;
static constexpr u16 BAR1 = 0x14;
static constexpr u16 BAR2 = 0x18;
static constexpr u16 BAR3 = 0x1C;
static constexpr u16 BAR4 = 0x20;
static constexpr u16 BAR5 = 0x24;

typedef struct {
	uint8_t bus;
	uint8_t dev;
	uint8_t func;
	uint8_t type;
	uint16_t device_id;
	uint16_t vendor_id;
	uint8_t base_class;
	uint8_t sub_class;
	uint8_t prog_interface;
	uint8_t rev_id; // Not read yet
	uint8_t irq;	// Not read yet
			// Bar stuff
} pci_device_t;

const pci_device_t* get_device_by_index(uint8_t index);
const pci_device_t* get_device_by_id(uint16_t device_id);
const pci_device_t* get_device_by_class(uint8_t base_class, uint8_t sub_class);
uint32_t
pci_config_read_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_config_write_dword(uint8_t bus,
			    uint8_t slot,
			    uint8_t func,
			    uint8_t offset,
			    uint32_t value);
void list_devices();
