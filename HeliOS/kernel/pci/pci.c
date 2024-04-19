// https://wiki.osdev.org/PCI
// https://www.pcilookup.com/
#include <kernel/asm.h>
#include <kernel/liballoc.h>
#include <kernel/pci/pci.h>
#include <stdio.h>

#define VENDOR_INVALID 0xFFFF

// TODO: Dynamiclly allocate
pci_device_t* devices[32];
uint8_t       device_idx = 0;

enum {
    BUS_COUNT = 8,
    DEV_COUNT = 32,
    FUNC_COUNT = 8,
};

enum {
    IOPORT_PCI_CFG_DATA = 0xCFC,
    IOPORT_PCI_CFG_ADDR = 0xCF8,
};

enum dev_type {
    GENERIC = 0x0,
    PCI_PCI_BRIDGE = 0x1,
    CARD_BUS_BRIDGE = 0x2,
};

pci_device_t* get_device_by_index(uint8_t index)
{
    if (index > device_idx)
        return NULL;
    else
        return devices[index];
}

pci_device_t* get_device_by_id(uint16_t device_id)
{
    for (uint8_t i = 0; i <= device_idx; i++) {
        if (devices[i]->device_id == device_id) return devices[i];
    }
    return NULL;
}

static const uint32_t pci_config_read_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    uint32_t address;
    uint32_t lbus = (uint32_t)bus;
    uint32_t lslot = (uint32_t)slot;
    uint32_t lfunc = (uint32_t)func;
    uint16_t tmp = 0;

    // Create configuration
    address = (uint32_t)((lbus << 16) | (lslot << 11) | (lfunc << 8) | (offset & 0xFC)
        | ((uint32_t)(0x80000000)));

    outdword(IOPORT_PCI_CFG_ADDR, address);
    return indword(IOPORT_PCI_CFG_DATA);
}

void list_devices()
{
    puts("(bus, dev, func)");
    for (uint8_t i = 0; i < BUS_COUNT; i++) {
        for (uint8_t j = 0; j < DEV_COUNT; j++) {
            for (uint8_t k = 0; k < FUNC_COUNT; k++) {
                uint32_t val = pci_config_read_word(i, j, k, 0);
                if ((val & 0xFFFF) == VENDOR_INVALID) continue;

                printf("(%d, %d, %d) 0x%X\n", i, j, k, val);
                pci_device_t* dev = (pci_device_t*)kmalloc(sizeof(pci_device_t));
                dev->bus = i;
                dev->dev = j;
                dev->func = k;
                dev->device_id = val >> 16;
                dev->vendor_id = val & 0xFFFF;
                val = pci_config_read_word(i, j, k, 0x8);
                dev->base_class = val >> 24;
                dev->sub_class = (val >> 16) & 0xFF;
                dev->prog_interface = (val >> 8) & 0xFF;
                // printf("bc: 0x%X, sc: 0x%X\n", dev->base_class, dev->sub_class);
                // printf("PIf: 0x%X\n", dev->prog_interface);
                val = pci_config_read_word(i, j, k, 0xC);
                dev->type = (val >> 16) & 0xFF;
                // printf("type: 0x%X\n", dev->type);
                devices[device_idx++] = dev;
            }
        }
    }
}
