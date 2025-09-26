/**
 * @file drivers/ata/device.c
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

#include <drivers/ata/ata.h>
#include <drivers/ata/controller.h>
#include <drivers/ata/device.h>
#include <drivers/ata/partition.h>
#include <kernel/timer.h>
#include <lib/log.h>
#include <stdint.h>

static bool device_identify(sATADevice* device, uint8_t cmd);

// TODO: Clean this up a bit.
//       Also need to implement the IDENTIFY struct.
//       Also need to support ATAPI
void device_init(sATADevice* device)
{
	uint16_t buffer[256];
	log_debug("Sending 'IDENTIFY DEVICE' to device %d", device->id);
	if (!device_identify(device, COMMAND_IDENTIFY)) {
		// if (!device_identify(device, COMMAND_IDENTIFY_PACKET)) {
		log_warn("Device %d not valid", device->id);
		return;
		// }
	}

	device->present = true;
	// Making sure device is not ATAPI
	if (!(device->info[0] & (1 << 15))) {
		device->sec_size = ATA_SEC_SIZE;
		device->rw_handler = ata_read_write;
		log_info("Device %d is an ATA-device", device->id);
		// Read partition table
		if (!ata_read_write(
			    device, OP_READ, buffer, 0, device->sec_size, 1)) {
			log_error("Unable to read partition table");
			device->present = false;
			return;
		}
		part_fill_partitions(device->part_table, buffer);
		part_print(device->part_table);
	}
}

static bool device_identify(sATADevice* device, uint8_t cmd)
{
	// ata-atapi-8 7.12
	sATAController* ctrl = device->ctrl;

	uint8_t device_select = device->id & SLAVE_BIT ? DRIVE_SLAVE :
							 DRIVE_MASTER;

	// printf("Selecting device %d, using value: 0x%X\n", device->id, device_select);
	ctrl_outb(ctrl, ATA_REG_DRIVE_SELECT, device_select);
	ctrl_wait(ctrl);

	/* disable interrupts */
	ctrl_outb(ctrl, ATA_REG_CONTROL, CTRL_NIEN);

	/* check whether the device exists */
	ctrl_outb(ctrl, ATA_REG_COMMAND, cmd);
	uint8_t status = ctrl_inb(ctrl, ATA_REG_STATUS);
	if (status == 0) {
		// printf("Device %d returned a status of 0\n", device->id);
		return false;
	}
	// Set Sectorcount, LBAlo, LBAmid, and LBAhi IO ports to 0
	ctrl_outb(ctrl, ATA_REG_SECTOR_COUNT, 0);
	ctrl_outb(ctrl, ATA_REG_ADDRESS1, 0);
	ctrl_outb(ctrl, ATA_REG_ADDRESS2, 0);
	ctrl_outb(ctrl, ATA_REG_ADDRESS3, 0);

	ctrl_outb(ctrl, ATA_REG_COMMAND, cmd);
	status = ctrl_inb(ctrl, ATA_REG_STATUS);
	if (status == 0) {
		log_warn("Device %d not found pt2", device->id);
		return false;
	}
	// TODO: Need to check if not ATA drive by checking LBAmid and LBAhi

	device_poll(device);
	// IDK if this second wait is needed
	ctrl_wait(ctrl);
	if (ctrl_inb(ctrl, ATA_REG_STATUS) & CMD_ST_ERROR) {
		log_error("Device %d has error 0x%X",
			  device->id,
			  ctrl_inb(ctrl, ATA_REG_ERROR));
		return false;
	}
	for (size_t i = 0; i < 256; i++) {
		device->info[i] = ctrl_inw(ctrl, ATA_REG_DATA);
	}

	if (!(device->info[49] & LBA_SUPPORT)) {
		log_error("Device %d does not support lba", device->id);
		return false;
	}

	// TODO: Need to support more than 128GB disks
	uint32_t lba_num = device->info[ATA_INFO_SECTORS_LOW] |
			   (device->info[ATA_INFO_SECTORS_HIGH] << 16);
	log_info("Device %d LBA support: 0x%X", device->id, lba_num);
	return true;
}

bool device_poll(sATADevice* device)
{
	uint32_t elapsed = 0;
	sATAController* ctrl = device->ctrl;
	uint8_t status = ctrl_inb(ctrl, ATA_REG_STATUS);
	while (status & CMD_ST_BUSY && !(status & CMD_ST_DRQ) &&
	       elapsed < 100000) {
		if (status & CMD_ST_ERROR || status & CMD_ST_DISK_FAULT)
			return false;
		elapsed++;
		status = ctrl_inb(ctrl, ATA_REG_STATUS);
	}
	if (elapsed >= 100000) return false;
	return true;
}
