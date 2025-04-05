// https://wiki.osdev.org/ATA_PIO_Mode#28_bit_PIO
#include <drivers/ata/ata.h>
#include <drivers/ata/controller.h>
#include <drivers/ata/device.h>
#include <stdio.h>

static uint16_t get_command(sATADevice* device, uint16_t op);
static bool setup_command(sATADevice* device, uint32_t lba, size_t sec_count,
                          uint16_t cmd);

// TODO: Add DMA support, currently just PIO
//       Also need support for ATAPI
bool ata_read_write(sATADevice* device, uint16_t op, void* buffer, uint32_t lba,
                    size_t sec_size, size_t sec_count)
{
    sATAController* ctrl = device->ctrl;
    uint16_t command = get_command(device, op);
    if (!command) return false;

    // bool st = setup_command(device, lba, secCount, command);
    // PIO Transfer:

    device_poll(device);
    // printf("Selecting Device %d using 0x%X\n", device->id,
    // 0xE0 | ((device->id & SLAVE_BIT) << 4) | ((lba >> 24) & 0x0F));
    ctrl_outb(ctrl, ATA_REG_DRIVE_SELECT,
              0xE0 | ((device->id & SLAVE_BIT) << 4) | ((lba >> 24) & 0x0F));
    ctrl_wait(ctrl);
    ctrl_outb(ctrl, ATA_REG_SECTOR_COUNT, (unsigned char)sec_count);
    ctrl_outb(ctrl, ATA_REG_ADDRESS1, (uint8_t)lba);
    ctrl_outb(ctrl, ATA_REG_ADDRESS2, (uint8_t)(lba >> 8));
    ctrl_outb(ctrl, ATA_REG_ADDRESS3, (uint8_t)(lba >> 16));
    ctrl_outb(ctrl, ATA_REG_COMMAND, command);
    ctrl_wait(ctrl);

    if (command == COMMAND_READ_SEC) {
        // For each sector we want to get
        for (size_t i = 0; i < sec_count; i++) {
            // First we poll the device to wait for it to be ready
            if (!device_poll(device)) {
                printf("Polling failed for device %d\n", device->id);
                return false;
            }
            // Weird casting to appease the clangd gods
            ctrl_inws(ctrl, ATA_REG_DATA,
                      (void*)((uintptr_t)buffer + (i * sec_size)),
                      sec_size / sizeof(uint16_t));
            // Flush cache
            ctrl_outb(ctrl, ATA_REG_COMMAND, COMMAND_CACHE_FLUSH);
            device_poll(device);
        }
    } else if (command == COMMAND_WRITE_SEC) {
        // For each sector we want to get
        for (size_t i = 0; i < sec_count; i++) {
            // First we poll the device to wait for it to be ready
            if (!device_poll(device)) {
                printf("Polling failed for device %d\n", device->id);
                return false;
            }
            ctrl_outws(ctrl, ATA_REG_DATA, buffer, sec_size / sizeof(uint16_t));
        }
        // Flush cache
        ctrl_outb(ctrl, ATA_REG_COMMAND, COMMAND_CACHE_FLUSH);
        device_poll(device);
    }

    return true;
}

static uint16_t get_command(sATADevice* device, uint16_t op)
{
    switch (op) {
    case OP_READ:
        return COMMAND_READ_SEC;
    case OP_WRITE:
        return COMMAND_WRITE_SEC;
    case OP_PACKET: // TODO: Figure out wtf this is
        return COMMAND_PACKET;
    }
    // IDK just gonna return zero if not valid
    return 0;
}

static bool setup_command(sATADevice* device, uint32_t lba, size_t sec_count,
                          uint16_t cmd)
{
    sATAController* ctrl = device->ctrl;
    return true;
}
