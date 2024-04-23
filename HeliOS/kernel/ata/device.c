#include <kernel/ata/ata.h>
#include <kernel/ata/controller.h>
#include <kernel/ata/device.h>
#include <kernel/ata/partition.h>
#include <kernel/timer.h>
#include <stdio.h>

static bool device_identify(sATADevice* device, uint16_t cmd);

// TODO: Clean this up a bit.
//       Also need to implement the IDENTIFY struct.
//       Also need to support ATAPI
void device_init(sATADevice* device)
{
    uint16_t buffer[256];
    sATAController* ctrl = device->ctrl;
    // printf("Sending 'IDENTIFY DEVICE' to device %d\n", device->id);
    if (!device_identify(device, COMMAND_IDENTIFY)) {
        // if (!device_identify(device, COMMAND_IDENTIFY_PACKET)) {
        printf("Device %d not valid\n", device->id);
        return;
        // }
    }

    device->present = true;
    // Making sure device is not ATAPI
    if (!(device->info[0] & (1 << 15))) {
        device->sec_size = ATA_SEC_SIZE;
        device->rw_handler = ata_read_write;
        printf("Device %d is an ATA-device\n", device->id);
        // Read partition table
        if (!ata_read_write(device, OP_READ, buffer, 0, device->sec_size, 1)) {
            puts("Unable to read partition table");
            device->present = false;
            return;
        }
        // puts("Parsing partition table");
        // for (size_t i = 0; i < 256; i++) {
        //     if (buffer[i]) printf("0x%X ", buffer[i]);
        // }
        part_fill_partitions(device->part_table, buffer);
        part_print(device->part_table);
    }
}

static bool device_identify(sATADevice* device, uint16_t cmd)
{
    // ata-atapi-8 7.12
    sATAController* ctrl = device->ctrl;

    uint32_t device_select = device->id & SLAVE_BIT ? 0xB0 : 0xA0;

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
        printf("Device %d not found pt2\n", device->id);
        return false;
    }
    // TODO: Need to check if not ATA drive by checking LBAmid and LBAhi

    device_poll(device);
    // IDK if this second wait is needed
    ctrl_wait(ctrl);
    if (ctrl_inb(ctrl, ATA_REG_STATUS) & CMD_ST_ERROR) {
        printf("Device %d has error 0x%X\n", device->id, ctrl_inb(ctrl, ATA_REG_ERROR));
        return false;
    }
    for (size_t i = 0; i < 256; i++) {
        device->info[i] = ctrl_inw(ctrl, ATA_REG_DATA);
    }

    if (!(device->info[49] & (1 << 9))) {
        printf("Device %d does not support lba\n", device->id);
        return false;
    }

    uint32_t lba_num = device->info[60] | (device->info[61] << 16);
    printf("Device %d LBA support: 0x%X\n", device->id, lba_num);
    return true;
}

bool device_poll(sATADevice* device)
{
    uint32_t elapsed = 0;
    sATAController* ctrl = device->ctrl;
    uint8_t status = ctrl_inb(ctrl, ATA_REG_STATUS);
    while (status & CMD_ST_BUSY && !(status & CMD_ST_DRQ) && elapsed < ATA_WAIT_TIMEOUT) {
        if (status & CMD_ST_ERROR || status & CMD_ST_DISK_FAULT) return false;
        sleep(20);
        elapsed += 20;
        status = ctrl_inb(ctrl, ATA_REG_STATUS);
    }
    if (elapsed >= ATA_WAIT_TIMEOUT) return false;
    return true;
}
