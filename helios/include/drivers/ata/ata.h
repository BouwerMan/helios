/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <drivers/ata/controller.h>

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
bool ata_read_write(sATADevice* device, u16 op, void* buffer, u32 lba, size_t sec_size, size_t sec_count);
