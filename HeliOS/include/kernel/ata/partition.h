#pragma once
#include <stddef.h>
#include <stdint.h>

/* the number of partitions per disk */
// static const size_t PARTITION_COUNT = 4;
#define PARTITION_COUNT 4

/* represents a partition (in memory) */
typedef struct {
    uint8_t present;
    /* start sector */
    size_t start;
    /* sector count */
    size_t size;
} sPartition;

/**
 * Fills the partition-table with the given MBR
 *
 * @param table the table to fill
 * @param mbr the content of the first sector
 */
void part_fill_partitions(sPartition* table, void* mbr);

/**
 * Prints the given partition table
 *
 * @param table the tables to print
 */
void part_print(sPartition* table);
