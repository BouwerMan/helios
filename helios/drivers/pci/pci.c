/**
 * @file drivers/pci/pci.c
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

// https://wiki.osdev.org/PCI
// https://www.pcilookup.com/
#include <stdlib.h>

#include <arch/ports.h>
#include <drivers/pci/pci.h>

#include <util/log.h>

// TODO: Dynamiclly allocate
pci_device_t* devices[32];
uint8_t device_idx = 0;

const pci_device_t* get_device_by_index(uint8_t index)
{
	if (index > device_idx)
		return NULL;
	else
		return devices[index];
}

const pci_device_t* get_device_by_id(uint16_t device_id)
{
	for (uint8_t i = 0; i <= device_idx; i++) {
		if (devices[i]->device_id == device_id) return devices[i];
	}
	return NULL;
}

const pci_device_t* get_device_by_class(uint8_t base_class, uint8_t sub_class)
{
	for (uint8_t i = 0; i <= device_idx; i++) {
		if (devices[i]->base_class == base_class && devices[i]->sub_class == sub_class) return devices[i];
	}
	return NULL;
}

uint32_t pci_config_read_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
	uint32_t address;
	uint32_t lbus  = (uint32_t)bus;
	uint32_t lslot = (uint32_t)slot;
	uint32_t lfunc = (uint32_t)func;

	// Create configuration
	address = (uint32_t)((lbus << 16) | (lslot << 11) | (lfunc << 8) | (offset & 0xFC) | ((uint32_t)(0x80000000)));

	// Write out address
	outdword(IOPORT_PCI_CFG_ADDR, address);
	return indword(IOPORT_PCI_CFG_DATA);
}

void pci_config_write_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value)
{
	uint32_t address;
	uint32_t lbus  = (uint32_t)bus;
	uint32_t lslot = (uint32_t)slot;
	uint32_t lfunc = (uint32_t)func;

	// Create configuration
	address = (uint32_t)((lbus << 16) | (lslot << 11) | (lfunc << 8) | (offset & 0xFC) | ((uint32_t)(0x80000000)));

	outdword(IOPORT_PCI_CFG_ADDR, address);
	outdword(IOPORT_PCI_CFG_DATA, value);
}

void list_devices()
{
	log_info("(bus, dev, func)");
	for (uint8_t i = 0; i < BUS_COUNT; i++) {
		for (uint8_t j = 0; j < DEV_COUNT; j++) {
			for (uint8_t k = 0; k < FUNC_COUNT; k++) {
				uint32_t val = pci_config_read_dword(i, j, k, 0);
				if ((val & 0xFFFF) == VENDOR_INVALID) continue;

				log_info("\t(%d, %d, %d) 0x%X", i, j, k, val);
				pci_device_t* dev     = (pci_device_t*)kmalloc(sizeof(pci_device_t));
				dev->bus	      = i;
				dev->dev	      = j;
				dev->func	      = k;
				dev->device_id	      = (uint16_t)(val >> 16);
				dev->vendor_id	      = val & 0xFFFF;
				val		      = pci_config_read_dword(i, j, k, 0x8);
				dev->base_class	      = (uint8_t)(val >> 24);
				dev->sub_class	      = (val >> 16) & 0xFF;
				dev->prog_interface   = (val >> 8) & 0xFF;
				val		      = pci_config_read_dword(i, j, k, 0xC);
				dev->type	      = (val >> 16) & 0xFF;
				devices[device_idx++] = dev;
				val		      = pci_config_read_dword(i, j, k, 0x4);
				log_info("status and command: %x", val);
			}
		}
	}
}
