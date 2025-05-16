#include <drivers/ata/partition.h>
#include <util/log.h>

/* offset of partition-table in MBR */
static const size_t PART_TABLE_OFFSET = 0x1BE;

/* a partition on the disk */
typedef struct {
	/* Boot indicator bit flag: 0 = no, 0x80 = bootable (or "active") */
	uint8_t bootable;
	/* start: Cylinder, Head, Sector */
	uint8_t startHead;
	uint16_t startSector : 6, startCylinder : 10;
	uint8_t systemId;
	/* end: Cylinder, Head, Sector */
	uint8_t endHead;
	uint16_t endSector : 6, endCylinder : 10;
	/* Relative Sector (to start of partition -- also equals the partition's starting LBA value) */
	uint32_t start;
	/* Total Sectors in partition */
	uint32_t size;
} __attribute__((packed)) sDiskPart;

void part_fill_partitions(sPartition* table, void* mbr)
{
	sDiskPart* src = (sDiskPart*)((uintptr_t)mbr + PART_TABLE_OFFSET);
	for (size_t i = 0; i < PARTITION_COUNT; i++) {
		table->present = src->systemId != 0;
		table->start = src->start;
		table->size = src->size;
		table++;
		src++;
	}
}

void part_print(sPartition* table)
{
	for (size_t i = 0; i < PARTITION_COUNT; i++) {
		log_info("%zu: present=%d start=%zu size=%zu", i,
			 table->present, table->start, table->size);
		table++;
	}
}
