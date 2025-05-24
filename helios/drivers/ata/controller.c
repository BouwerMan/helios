/**
 * @file drivers/ata/controller.c
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

#include <stddef.h>

#include <drivers/ata/controller.h>
#include <drivers/ata/device.h>
#include <drivers/pci/pci.h>
#include <kernel/liballoc.h>
#include <kernel/memory/vmm.h>

#include <util/log.h>

/* port-bases */
static const int PORTBASE_PRIMARY = 0x1F0;
static const int PORTBASE_SECONDARY = 0x170;

static const int IDE_CTRL_CLASS = 0x01;
static const int IDE_CTRL_SUBCLASS = 0x01;

static const pci_device_t* ide_ctrl;

static sATAController ctrls[2];

void ctrl_init()
{
	ide_ctrl = get_device_by_class(IDE_CTRL_CLASS, IDE_CTRL_SUBCLASS);
	if (!ide_ctrl) {
		log_error("Could not get IDE controller.");
		return;
	}

	// maybe needed to check if I/O space is enabled?
	uint32_t status = pci_config_read_dword(ide_ctrl->bus, ide_ctrl->dev, ide_ctrl->func, 0x04);
	if ((uint8_t)status == 0xFF) {
		log_error("Floating IDE bus");
		return;
	}

	ctrls[0].id = DEVICE_PRIMARY;
	ctrls[0].irq = CTRL_IRQ_BASE;
	ctrls[0].port_base = PORTBASE_PRIMARY;
	ctrls[0].IO_port_base = IO_PORTBASE_PRIMARY;

	ctrls[1].id = DEVICE_SECONDARY;
	ctrls[1].irq = CTRL_IRQ_BASE + 1;
	ctrls[1].port_base = PORTBASE_SECONDARY;
	ctrls[1].IO_port_base = IO_PORTBASE_SECONDARY;

	uint32_t bar4 = pci_config_read_dword(ide_ctrl->bus, ide_ctrl->dev, ide_ctrl->func, BAR4);
	if ((bar4 & 1) == 1) {
		log_debug("BAR4: %x, actual base: %x", bar4, bar4 & 0xFFFFFFFC);
		ctrls[0].bmr_base = (uint16_t)(bar4 & 0xFFFFFFFC);
		ctrls[1].bmr_base = (uint16_t)((bar4 & 0xFFFFFFFC) + 0x8);
	}
	log_debug("Setting Bus Master Enable for PCI: bus: %x, dev: %x, func: %x", ide_ctrl->bus, ide_ctrl->dev,
		  ide_ctrl->func);
	uint32_t cfg = pci_config_read_dword(ide_ctrl->bus, ide_ctrl->dev, ide_ctrl->func, 0x04);
	log_debug("PCI Configuration: %x", cfg);
	// Set Bus Master enable
	pci_config_write_dword(ide_ctrl->bus, ide_ctrl->dev, ide_ctrl->func, 0x04, cfg | 0x4);
	log_debug("PCI Configuration: %x", cfg);
	cfg = pci_config_read_dword(ide_ctrl->bus, ide_ctrl->dev, ide_ctrl->func, 0x3C);
	log_debug("PCI Interrupt stuff, %x", cfg);
	for (size_t i = 0; i < 2; i++) {
		log_info("Initializing controller: %d", ctrls[i].id);

		ctrls[i].use_irq = false;
		ctrls[i].use_dma = true;
		ctrls[i].ide_ctrl = ide_ctrl;

		if (ctrls[i].use_dma) {
			// TODO: Make sure this is in low 4GB
			ctrls[i].prdt = vmm_alloc_pages(1, false);
			log_debug("prdt: %p", (void*)ctrls[i].prdt);
			// TODO: clean up on alloc fail
			uint64_t full_addr = (uintptr_t)vmm_translate(ctrls[i].prdt);
			if (full_addr >= (1ULL << 32)) {
				// handle error or log warning: address cannot be DMA'd
				log_error("PRDT is not in valid location: %lx", full_addr);
			}
			uint32_t prdt_phys = (uint32_t)full_addr;
			log_debug("Writing PRDT addr: %x", prdt_phys);
			ctrl_bmr_outd(ctrls + i, BMR_REG_PRDT, prdt_phys);
		}

		// Init attached drives, beginning with slave
		for (short int j = 1; j >= 0; j--) {
			ctrls[i].devices[j].present = false;
			ctrls[i].devices[j].id = (uint8_t)(i * 2 + (unsigned short)j);
			ctrls[i].devices[j].ctrl = ctrls + i;
			device_init(ctrls[i].devices + j);
		}
	}
}

sATADevice* ctrl_get_device(uint8_t id)
{
	return ctrls[id / 2].devices + id % 2;
}

void ctrl_outb(sATAController* ctrl, uint16_t reg, uint8_t value)
{
	outb(ctrl->port_base + reg, value);
}

uint8_t ctrl_inb(sATAController* ctrl, uint16_t reg)
{
	return inb(ctrl->port_base + reg);
}
uint16_t ctrl_inw(sATAController* ctrl, uint16_t reg)
{
	return inw(ctrl->port_base + reg);
}

void ctrl_inws(sATAController* ctrl, uint16_t reg, uint16_t* buff, size_t count)
{
	for (size_t i = 0; i < count; i++)
		buff[i] = inw(ctrl->port_base + reg);
}

void ctrl_outws(sATAController* ctrl, uint16_t reg, const uint16_t* buff, size_t count)
{
	for (size_t i = 0; i < count; i++)
		outword(ctrl->port_base + reg, buff[i]);
}

void ctrl_wait(sATAController* ctrl)
{
	inb(ctrl->port_base + ATA_REG_STATUS);
	inb(ctrl->port_base + ATA_REG_STATUS);
	inb(ctrl->port_base + ATA_REG_STATUS);
	inb(ctrl->port_base + ATA_REG_STATUS);
}
