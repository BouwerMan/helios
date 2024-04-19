#pragma once
#include <stdint.h>
// TODO: Should these be moved out of the header file? Can't imagine many other files will need
// these

// TODO: Add enum names?
enum ATAStatus {
    ATA_SR_BSY = 0x80,  // Busy
    ATA_SR_DRDY = 0x40, // Drive ready
    ATA_SR_DF = 0x20,   // Drive write fault
    ATA_SR_DSC = 0x10,  // Drive seek complete
    ATA_SR_DRQ = 0x08,  // Data request ready
    ATA_SR_CORR = 0x04, // Corrected data
    ATA_SR_IDX = 0x02,  // Index
    ATA_SR_ERR = 0x01,  // Error
};

enum ATAErrors {
    ATA_ER_BBK = 0x80,   // Bad block
    ATA_ER_UNC = 0x40,   // Uncorrectable data
    ATA_ER_MC = 0x20,    // Media changed
    ATA_ER_IDNF = 0x10,  // ID mark not found
    ATA_ER_MCR = 0x08,   // Media change request
    ATA_ER_ABRT = 0x04,  // Command aborted
    ATA_ER_TK0NF = 0x02, // Track 0 not found
    ATA_ER_AMNF = 0x01,  // No address mark
};

enum ATACommands {
    ATA_CMD_READ_PIO = 0x20,
    ATA_CMD_READ_PIO_EXT = 0x24,
    ATA_CMD_READ_DMA = 0xC8,
    ATA_CMD_READ_DMA_EXT = 0x25,
    ATA_CMD_WRITE_PIO = 0x30,
    ATA_CMD_WRITE_PIO_EXT = 0x34,
    ATA_CMD_WRITE_DMA = 0xCA,
    ATA_CMD_WRITE_DMA_EXT = 0x35,
    ATA_CMD_CACHE_FLUSH = 0xE7,
    ATA_CMD_CACHE_FLUSH_EXT = 0xEA,
    ATA_CMD_PACKET = 0xA0,
    ATA_CMD_IDENTIFY_PACKET = 0xA1,
    ATA_CMD_IDENTIFY = 0xEC,

};
// TODO: gave up on the enum translation from the IDE osdev wiki, once I understand these values I
//       should be able to translate fully.

#define ATAPI_CMD_READ  0xA8
#define ATAPI_CMD_EJECT 0x1B

#define ATA_IDENT_DEVICETYPE   0
#define ATA_IDENT_CYLINDERS    2
#define ATA_IDENT_HEADS        6
#define ATA_IDENT_SECTORS      12
#define ATA_IDENT_SERIAL       20
#define ATA_IDENT_MODEL        54
#define ATA_IDENT_CAPABILITIES 98
#define ATA_IDENT_FIELDVALID   106
#define ATA_IDENT_MAX_LBA      120
#define ATA_IDENT_COMMANDSETS  164
#define ATA_IDENT_MAX_LBA_EXT  200

#define IDE_ATA   0x00
#define IDE_ATAPI 0x01

#define ATA_MASTER 0x00
#define ATA_SLAVE  0x01

#define ATA_REG_DATA       0x00
#define ATA_REG_ERROR      0x01
#define ATA_REG_FEATURES   0x01
#define ATA_REG_SECCOUNT0  0x02
#define ATA_REG_LBA0       0x03
#define ATA_REG_LBA1       0x04
#define ATA_REG_LBA2       0x05
#define ATA_REG_HDDEVSEL   0x06
#define ATA_REG_COMMAND    0x07
#define ATA_REG_STATUS     0x07
#define ATA_REG_SECCOUNT1  0x08
#define ATA_REG_LBA3       0x09
#define ATA_REG_LBA4       0x0A
#define ATA_REG_LBA5       0x0B
#define ATA_REG_CONTROL    0x0C
#define ATA_REG_ALTSTATUS  0x0C
#define ATA_REG_DEVADDRESS 0x0D

// Channels:
#define ATA_PRIMARY   0x00
#define ATA_SECONDARY 0x01

// Directions:
#define ATA_READ  0x00
#define ATA_WRITE 0x01

void ide_initialize(uint16_t BAR0, uint16_t BAR1, uint16_t BAR2, uint16_t BAR3, uint16_t BAR4);
