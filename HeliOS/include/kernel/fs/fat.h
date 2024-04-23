#pragma once
#include <kernel/ata/controller.h>
#include <kernel/fs/vfs.h>

// enum {
//     FAT12,
//     FAT16,
//     FAT32,
//     ExFat,
// };

// TODO: put uint8_t types in the structs
typedef struct fat_extBS_32 {
    // extended fat32 stuff
    unsigned int table_size_32;
    unsigned short extended_flags;
    unsigned short fat_version;
    unsigned int root_cluster;
    unsigned short fat_info;
    unsigned short backup_BS_sector;
    unsigned char reserved_0[12];
    unsigned char drive_number;
    unsigned char reserved_1;
    unsigned char boot_signature;
    unsigned int volume_id;
    unsigned char volume_label[11];
    unsigned char fat_type_label[8];

} __attribute__((packed)) fat_extBS_32_t;

typedef struct fat_extBS_16 {
    // extended fat12 and fat16 stuff
    unsigned char bios_drive_num;
    unsigned char reserved1;
    unsigned char boot_signature;
    unsigned int volume_id;
    unsigned char volume_label[11];
    unsigned char fat_type_label[8];

} __attribute__((packed)) fat_extBS_16_t;

typedef struct fat_BS {
    unsigned char bootjmp[3];
    unsigned char oem_name[8];
    unsigned short bytes_per_sector;
    unsigned char sectors_per_cluster;
    unsigned short reserved_sector_count;
    unsigned char table_count;
    unsigned short root_entry_count;
    unsigned short total_sectors_16;
    unsigned char media_type;
    unsigned short table_size_16;
    unsigned short sectors_per_track;
    unsigned short head_side_count;
    unsigned int hidden_sector_count;
    unsigned int total_sectors_32;

    // this will be cast to it's specific type once the driver actually knows what type of FAT this
    // is.
    unsigned char extended_section[54];

} __attribute__((packed)) fat_BS_t;

typedef struct fat_filetable_s {
    char name[8];       //!< 8.3 Name
    char ext[3];        //!< 8.3 File Extension
    uint8_t attrib;     //!< File Attributes.
    uint8_t ntres;      //!< Reserved for NT - Set to 0
    uint8_t ctimems;    //!< 10ths of a second ranging from 0-199 (2 seconds)
    uint16_t ctime;     //!< Creation Time
    uint16_t cdate;     //!< Creation Date
    uint16_t adate;     //!< Accessed Date. No Time feild though
    uint16_t clusterHi; //!< High Cluster. 0 for FAT12 and FAT16
    uint16_t mtime;     //!< Last Modified Time
    uint16_t mdate;     //!< Last Modified Date
    uint16_t cluster;   //!< Low Word of First cluster
    uint32_t size;      //!< Size of file
} __attribute__((packed)) fat_filetable_t;

// TODO: Resize these fields
struct fat_fs {
    uint32_t lba_start;
    uint16_t total_sectors;
    uint16_t sector_size;
    uint16_t fat_size;
    uint16_t root_dir_sectors;
    uint16_t first_root_dir_sector;
    uint16_t first_data_sector;
    uint16_t data_sectors;
    uint16_t first_fat_sector;
    uint16_t total_clusters;
    uint8_t fat_type;
    sATADevice* device;
};

typedef struct fat_filetable_s fat_filetable;

void init_fat(sATADevice* device, uint32_t lba_start);
void* fat_open_file(const char* directory, const char* filename);
