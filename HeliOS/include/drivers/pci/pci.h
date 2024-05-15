#pragma once
#include <stddef.h>
#include <stdint.h>

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
    uint8_t irq;    // Not read yet
    // Bar stuff
} pci_device_t;

const pci_device_t* get_device_by_index(uint8_t index);
const pci_device_t* get_device_by_id(uint16_t device_id);
const pci_device_t* get_device_by_class(uint8_t base_class, uint8_t sub_class);
const uint32_t pci_config_read_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void list_devices();
