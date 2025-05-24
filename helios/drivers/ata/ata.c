/**
 * @file drivers/ata/ata.c
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

// https://wiki.osdev.org/ATA_PIO_Mode#28_bit_PIO
#include <limits.h>
#include <string.h>

#include <drivers/ata/ata.h>
#include <drivers/ata/controller.h>
#include <drivers/ata/device.h>
#include <kernel/liballoc.h>
#include <kernel/memory/pmm.h>
#include <kernel/memory/vmm.h>
#include <kernel/sys.h>

// Disabling debug log messages
#ifndef __ATA_DEBUG__
#undef LOG_LEVEL
#define LOG_LEVEL 1
#define FORCE_LOG_REDEF
#include <util/log.h>
#undef FORCE_LOG_REDEF
#else
#include <util/log.h>
#endif

static bool bmr_poll(sATADevice* device);
static uint16_t get_command(sATADevice* device, uint16_t op);
static bool program_ata_reg(sATADevice* device, uint32_t lba, size_t sec_count, uint16_t command);
static bool read_dma(sATADevice* device, uint16_t command, void* buffer, uint32_t lba, size_t sec_size,
		     size_t sec_count);

// TODO: Support for ATAPI
bool ata_read_write(sATADevice* device, uint16_t op, void* buffer, uint32_t lba, size_t sec_size, size_t sec_count)
{
	log_debug("Trying to access lba: %x, sec_count: %zx, sec_size: %zx", lba, sec_count, sec_size);
	if (sec_size * sec_count > 65536) {
		log_error("DMA doesn't support more than 64KiB");
		return false;
	}
	sATAController* ctrl = device->ctrl;
	uint16_t command = get_command(device, op);
	if (!command) return false;

	switch (command) {
	case COMMAND_READ_SEC:
		// For each sector we want to get
		program_ata_reg(device, lba, sec_count, command);
		for (size_t i = 0; i < sec_count; i++) {
			// First we poll the device to wait for it to be ready
			if (!device_poll(device)) {
				printf("Polling failed for device %d", device->id);
				return false;
			}
			// Weird casting to appease the clangd gods
			ctrl_inws(ctrl, ATA_REG_DATA, (void*)((uintptr_t)buffer + (i * sec_size)),
				  sec_size / sizeof(uint16_t));
			// Flush cache
			ctrl_outb(ctrl, ATA_REG_COMMAND, COMMAND_CACHE_FLUSH);
			device_poll(device);
		}
		break;
	case COMMAND_READ_DMA:
		return read_dma(device, command, buffer, lba, sec_size, sec_count);
	case COMMAND_WRITE_SEC:
		program_ata_reg(device, lba, sec_count, command);
		// For each sector we want to get
		for (size_t i = 0; i < sec_count; i++) {
			// First we poll the device to wait for it to be ready
			if (!device_poll(device)) {
				printf("Polling failed for device %d", device->id);
				return false;
			}
			ctrl_outws(ctrl, ATA_REG_DATA, buffer, sec_size / sizeof(uint16_t));
		}
		// Flush cache
		ctrl_outb(ctrl, ATA_REG_COMMAND, COMMAND_CACHE_FLUSH);
		device_poll(device);
		break;
	}

	return true;
}

static bool program_ata_reg(sATADevice* device, uint32_t lba, size_t sec_count, uint16_t command)
{
	sATAController* ctrl = device->ctrl;
	if (!device_poll(device)) return false;

	outb(ctrl->port_base + ATA_REG_DRIVE_SELECT, 0xE0 | ((device->id & SLAVE_BIT) << 4) | ((lba >> 24) & 0x0F));
	ctrl_wait(ctrl);

	log_debug("sending sec_count: %x", (uint8_t)sec_count);
	outb(ctrl->port_base + ATA_REG_SECTOR_COUNT, (uint8_t)sec_count);
	outb(ctrl->port_base + ATA_REG_ADDRESS1, (uint8_t)lba);
	outb(ctrl->port_base + ATA_REG_ADDRESS2, (uint8_t)(lba >> 8));
	outb(ctrl->port_base + ATA_REG_ADDRESS3, (uint8_t)(lba >> 16));

	log_debug("Enabling drive interrupts");
	outb(ctrl->IO_port_base + 0, 0x00);

	log_debug("Sending command: %x", command);
	outb(ctrl->port_base + ATA_REG_COMMAND, (uint8_t)command);
	ctrl_wait(ctrl);

	return true;
}

static uint16_t get_command(sATADevice* device, uint16_t op)
{
	bool use_dma = device->ctrl->use_dma;
	switch (op) {
	case OP_READ:
		return use_dma ? COMMAND_READ_DMA : COMMAND_READ_SEC;
	case OP_WRITE:
		return COMMAND_WRITE_SEC;
	case OP_PACKET: // TODO: Figure out wtf this is
		return COMMAND_PACKET;
	}
	// IDK just gonna return zero if not valid
	return 0;
}

static bool bmr_poll(sATADevice* device)
{
	sATAController* ctrl = device->ctrl;
	int timeout = 100000;
	while (timeout--) {
		uint8_t status = inb(ctrl->bmr_base + BMR_REG_STATUS);
		if (status & BMR_STATUS_IRQ) {
			// Acknowledge and stop
			outb(ctrl->bmr_base + BMR_REG_STATUS, BMR_STATUS_IRQ);
			outb(ctrl->bmr_base + BMR_REG_COMMAND, 0);
			log_debug("IRQ was risen and acknowledged");
			return true;
		}

		// If engine stopped but IRQ not set
		if ((status & BMR_STATUS_DMA) == 0) {
			// Check ATA drive status
			uint8_t ata = inb(ctrl->port_base + ATA_REG_STATUS);
			if ((ata & CMD_ST_BUSY) == 0 && (ata & CMD_ST_DRQ) == 0 && (ata & CMD_ST_ERROR) == 0) {
				log_warn("DMA completed but no IRQ raised — fallback path");
				outb(ctrl->bmr_base + BMR_REG_COMMAND, 0);
				// TODO: This warrants more intensive error handling
				return false;
			}
		}
	}

	return false;
}

static bool read_dma(sATADevice* device, uint16_t command, void* buffer, uint32_t lba, size_t sec_size,
		     size_t sec_count)
{
	sATAController* ctrl = device->ctrl;
	struct PRDT* prdt = ctrl->prdt;
	const uint16_t bmr_base = ctrl->bmr_base;

	size_t pages = (sec_count * sec_size / PAGE_SIZE) + 1;
	log_debug("Allocating dma buffer of %d pages", pages);
	// TODO: Make sure dma_buffer is < 4GB.
	void* dma_buffer = vmm_alloc_pages(pages, true);
	if (!dma_buffer) goto clean;

	uint64_t full_addr = (uintptr_t)vmm_translate(dma_buffer);
	if (full_addr >= (1ULL << 32)) {
		// handle error or log warning: address cannot be DMA'd
		log_error("PRDT buffer is not in valid location: %lx", full_addr);
	}
	prdt->addr = (uint32_t)full_addr;
	// TODO: Put size limits
	prdt->size = (uint16_t)(sec_count * sec_size);
	prdt->flags |= PRDT_EOT;

	// 1. Set command register to 0 to halt in-flight DMA
	outb(bmr_base + BMR_REG_COMMAND, 0);

	// 2. Clearing interrupt and error flags
	uint8_t bmr_st = inb(bmr_base + BMR_REG_STATUS);
	log_debug("bmr_st: %x", bmr_st);
	outb(bmr_base + BMR_REG_STATUS, bmr_st | BMR_STATUS_IRQ | BMR_STATUS_ERROR);

	// 3. Write PRDT pointer
	uint64_t full_prdt_addr = (uintptr_t)vmm_translate(prdt);
	if (full_addr >= (1ULL << 32)) {
		// handle error or log warning: address cannot be DMA'd
		log_error("PRDT is not in valid location: %lx", full_prdt_addr);
	}
	uint32_t prdt_phys = (uint32_t)full_prdt_addr;
	log_debug("Writing PRDT addr: %x", prdt_phys);
	outdword(bmr_base + BMR_REG_PRDT, prdt_phys);

	// 4. and 5. Program ATA registers, includes enabling interrupts
	if (!program_ata_reg(device, lba, sec_count, command)) goto clean_prog;

	// 6. Waiting for BSY=0 and DQR=1. Haven't actually caught it not being the case. I assume it is just fast :/
	if (!device_poll(device)) goto clean_prog;

	// 7. Start DMA
	outb(bmr_base + BMR_REG_COMMAND, BMR_CMD_START);

	// 8. and 9. Waiting for IRQ, then ACK and stop
	if (!bmr_poll(device)) goto clean_prog;
	log_debug("DMA should be complete");

	// TODO: Maybe do some stuff with caching dma_buffer
	memcpy(buffer, dma_buffer, prdt->size);
	log_debug("Freeing dma_buffer");
	vmm_free_pages(dma_buffer, pages);
	return true;

clean_prog:
	vmm_free_pages(dma_buffer, pages);
clean:
	return false;
}
