#include <kernel/ata/device.h>
#include <kernel/fs/fat.h>
#include <stdio.h>

void init_fat(sATADevice* device)
{
    uint16_t buffer[256] = { 0 };
    device->rw_handler(device, OP_READ, buffer, 0x63, device->sec_size, 1);
    for (size_t i = 0; i < 256; i++) {
        if (buffer[i]) printf("0x%X ", buffer[i]);
    }
}
