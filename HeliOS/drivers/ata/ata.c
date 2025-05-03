// https://wiki.osdev.org/ATA_PIO_Mode#28_bit_PIO
#include <drivers/ata/ata.h>
#include <drivers/ata/controller.h>
#include <drivers/ata/device.h>
#include <kernel/liballoc.h>
#include <kernel/memory/pmm.h>
#include <kernel/memory/vmm.h>
#include <kernel/sys.h>
#include <limits.h>
#include <string.h>

// Disabling debug log messages
#ifndef __ATA_DEBUG__
#define LOG_LEVEL 1
#define FORCE_LOG_REDEF
#include <util/log.h>
#undef FORCE_LOG_REDEF
#else
#include <util/log.h>
#endif

static void bmr_poll(sATADevice* device);
static uint16_t get_command(sATADevice* device, uint16_t op);
static bool setup_command(sATADevice* device, uint32_t lba, size_t sec_count, uint16_t cmd);
static void program_ata_reg(sATADevice* device, uint32_t lba, size_t sec_count, uint16_t command);
static void read_dma(sATADevice* device, uint16_t command, void* buffer, uint32_t lba, size_t sec_size,
		     size_t sec_count);

// TODO: Add DMA support, currently just PIO
//       Also need support for ATAPI
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

	// bool st = setup_command(device, lba, secCount, command);
	// PIO Transfer:

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
		read_dma(device, command, buffer, lba, sec_size, sec_count);
		break;
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

static void program_ata_reg(sATADevice* device, uint32_t lba, size_t sec_count, uint16_t command)
{
	sATAController* ctrl = device->ctrl;
	device_poll(device);
	// printf("Selecting Device %d using 0x%X\n", device->id,
	// 0xE0 | ((device->id & SLAVE_BIT) << 4) | ((lba >> 24) & 0x0F));
	ctrl_outb(ctrl, ATA_REG_DRIVE_SELECT, 0xE0 | ((device->id & SLAVE_BIT) << 4) | ((lba >> 24) & 0x0F));
	ctrl_wait(ctrl);
	log_debug("sending sec_count: %x", (uint8_t)sec_count);
	ctrl_outb(ctrl, ATA_REG_SECTOR_COUNT, (uint8_t)sec_count);
	ctrl_outb(ctrl, ATA_REG_ADDRESS1, (uint8_t)lba);
	ctrl_outb(ctrl, ATA_REG_ADDRESS2, (uint8_t)(lba >> 8));
	ctrl_outb(ctrl, ATA_REG_ADDRESS3, (uint8_t)(lba >> 16));
	log_debug("Enabling drive interrupts");
	outb(ctrl->IO_port_base + 0, 0x00);
	log_debug("Sending command: %x", command);
	ctrl_outb(ctrl, ATA_REG_COMMAND, command);
	ctrl_wait(ctrl);
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

static bool setup_command(sATADevice* device, uint32_t lba, size_t sec_count, uint16_t cmd)
{
	sATAController* ctrl = device->ctrl;
	return true;
}

static void bmr_poll(sATADevice* device)
{
	sATAController* ctrl = device->ctrl;
	long long int timeout = LONG_LONG_MAX;
	while (timeout--) {
		uint8_t status = ctrl_bmr_inb(ctrl, BMR_REG_STATUS);
		if (status & BMR_STATUS_IRQ) {
			// Acknowledge and stop
			ctrl_bmr_outb(ctrl, BMR_REG_STATUS, BMR_STATUS_IRQ);
			ctrl_bmr_outb(ctrl, BMR_REG_COMMAND, 0);
			log_debug("IRQ was risen and acknowledged");
			return;
		}

		// If engine stopped but IRQ not set
		if ((status & BMR_STATUS_DMA) == 0) {
			// Check ATA drive status
			uint8_t ata = ctrl_inb(ctrl, ATA_REG_STATUS);
			if ((ata & CMD_ST_BUSY) == 0 && (ata & CMD_ST_DRQ) == 0 && (ata & CMD_ST_ERROR) == 0) {
				log_warn("DMA completed but no IRQ raised — fallback path");
				ctrl_bmr_outb(ctrl, BMR_REG_COMMAND, 0);
				return;
			}
		}
	}

	panic("DMA did not complete in time");
}

static bool wait_for_drq(sATADevice* dev)
{
	for (int i = 0; i < 100000; ++i) {
		uint8_t s = ctrl_inb(dev->ctrl, ATA_REG_STATUS);
		if (!(s & CMD_ST_BUSY) && (s & CMD_ST_DRQ)) return true;
	}
	return false;
}

static void read_dma(sATADevice* device, uint16_t command, void* buffer, uint32_t lba, size_t sec_size,
		     size_t sec_count)
{
	sATAController* ctrl = device->ctrl;
	struct PRDT* prdt = ctrl->prdt;
	int pages = (sec_count * sec_size / PAGE_SIZE) + 1;
	log_debug("Allocating dma buffer of %d pages", pages);
	void* dma_buffer = vmm_alloc_pages(pages, true);
	// prdt->addr = (uint64_t)dma_buffer - KERNEL_HEAP_BASE;
	prdt->addr = (uint32_t)vmm_translate(dma_buffer);
	prdt->size = sec_count * sec_size;
	// prdt->size = 4096;
	prdt->flags |= PRDT_EOT;
	log_debug("DMA virtual: %p, DMA phys: %x, size: %d, flags: %x", dma_buffer, prdt->addr, prdt->size,
		  prdt->flags);
	log_debug("prdt location: %p, first dword: %x, second dword: %x", prdt, *prdt,
		  *(struct PRDT*)((uintptr_t)prdt + 4));
	log_debug("PRDT phys: 0x%p addr: 0x%x size: %x flags: %x", vmm_translate(prdt), prdt->addr, prdt->size,
		  prdt->flags);
	// Set command register to 0 to halt in-flight DMA
	ctrl_bmr_outb(ctrl, BMR_REG_COMMAND, 0);
	// Write back the status, should clear it's interrupt flag
	uint8_t bmr_st = ctrl_bmr_inb(ctrl, BMR_REG_STATUS);
	log_debug("bmr_st: %x", bmr_st);
	ctrl_bmr_outb(ctrl, BMR_REG_STATUS, bmr_st | BMR_STATUS_IRQ | BMR_STATUS_ERROR);
	// ctrl_bmr_outd(ctrl, BMR_REG_PRDT, (uint32_t)prdt);
	uint32_t prdt_phys = (uintptr_t)vmm_translate(prdt);
	log_debug("Writing PRDT addr: %x", prdt_phys);
	ctrl_bmr_outd(ctrl, BMR_REG_PRDT, prdt_phys);
	// Now we program ata registers
	uint8_t status = ctrl_inb(ctrl, ATA_REG_STATUS);
	log_debug("Status just before DMA: %x", status);
	program_ata_reg(device, lba, sec_count, command);
	// ctrl_bmr_outb(ctrl, BMR_REG_COMMAND, BMR_CMD_START);
	status = ctrl_inb(ctrl, ATA_REG_STATUS);
	log_debug("Status of ATA drive going into polling: %x", status);
	device_poll(device);
	// log_debug("Started DMA: %x", ctrl_bmr_inb(ctrl, BMR_REG_COMMAND));
	status = ctrl_inb(ctrl, ATA_REG_STATUS);
	log_debug("Status of ATA drive going into BMR polling: %x", status);

	wait_for_drq(device);
	status = ctrl_inb(ctrl, ATA_REG_STATUS);
	log_debug("Status before writing BMR start: %x", status);
	ctrl_bmr_outb(ctrl, BMR_REG_COMMAND, BMR_CMD_START);
	bmr_st = ctrl_bmr_inb(ctrl, BMR_REG_STATUS);
	log_debug("bmr_st: %x", bmr_st);
	bmr_st = ctrl_bmr_inb(ctrl, BMR_REG_STATUS);
	log_debug("bmr_st: %x", bmr_st);
	bmr_poll(device);
	log_debug("DMA should be complete");
	// TODO: Maybe do some stuff with caching dma_buffer
	memcpy(buffer, dma_buffer, prdt->size);
	log_debug("Freeing pages");
	vmm_free_pages(dma_buffer, pages);
	log_debug("Leaving read_dma()");
}
