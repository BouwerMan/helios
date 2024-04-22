#pragma once
#include <kernel/ata/controller.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Reads or writes from/to an ATA-device
 *
 * @param device the device
 * @param op the operation: OP_READ, OP_WRITE or OP_PACKET
 * @param buffer the buffer to write to
 * @param lba the block-address to start at
 * @param secSize the size of a sector
 * @param secCount number of sectors
 * @return true on success
 */
bool ata_read_write(
    sATADevice* device, uint16_t op, void* buffer, uint32_t lba, size_t sec_size, size_t sec_count);
