#pragma once
#include <drivers/ata/partition.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    DEVICE_PRIMARY = 0,
    DEVICE_SECONDARY = 1,
};

/* device-identifier */
enum {
    DEVICE_PRIM_MASTER = 0,
    DEVICE_PRIM_SLAVE = 1,
    DEVICE_SEC_MASTER = 2,
    DEVICE_SEC_SLAVE = 3,
};

enum {
    BMR_REG_COMMAND = 0x0,
    BMR_REG_STATUS = 0x2,
    BMR_REG_PRDT = 0x4,
};

enum {
    BMR_STATUS_IRQ = 0x4,
    BMR_STATUS_ERROR = 0x2,
    BMR_STATUS_DMA = 0x1,
};

enum {
    BMR_CMD_START = 0x1,
    BMR_CMD_READ = 0x8,
};

static const int CTRL_IRQ_BASE = 14;

typedef struct sATAController sATAController;
typedef struct sATADevice sATADevice;
typedef bool (*fReadWrite)(
    sATADevice* device, uint16_t op, void* buffer, uint32_t lba, size_t secSize, size_t secCount);

struct sATADevice {
    /* the identifier; 0-3; bit0 set means slave */
    uint8_t id;
    /* whether the device exists and we can use it */
    uint8_t present;
    /* master / slave */
    uint8_t slave_bit;
    /* the sector-size */
    size_t sec_size;
    /* the ata-controller to which the device belongs */
    sATAController* ctrl;
    /* handler-function for reading / writing */
    fReadWrite rw_handler;
    /* various informations we got via IDENTIFY-command */
    uint16_t info[256];
    // sATAIdentify info;
    /* the partition-table */
    sPartition part_table[PARTITION_COUNT];
};

struct sATAController {
    uint8_t id;
    uint8_t use_irq;
    uint8_t use_dma;
    /* I/O-ports for the controllers */
    uint16_t port_base;
    /* I/O-ports for bus-mastering */
    uint16_t bmr_base;
    int irq;
    int irqsem;
    sATADevice devices[2];
};

void ctrl_init();

sATADevice* ctrl_get_device(uint8_t id);

void ctrl_outb(sATAController* ctrl, uint16_t reg, uint8_t value);

uint8_t ctrl_inb(sATAController* ctrl, uint16_t reg);
uint16_t ctrl_inw(sATAController* ctrl, uint16_t reg);

// in words
void ctrl_inws(sATAController* ctrl, uint16_t reg, uint16_t* buff, size_t count);

// out words
void ctrl_outws(sATAController* ctrl, uint16_t reg, const uint16_t* buff, size_t count);

/**
 * Performs a few io-port-reads (just to waste a bit of time ;))
 *
 * @param ctrl the controller
 */
void ctrl_wait(sATAController* ctrl);
