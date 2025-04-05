#pragma once
#include <drivers/ata/controller.h>
#include <drivers/fs/vfs.h>

#define FAT_ROOT_CLUSTER 2

enum FAT_VALUES {
    FAT_BAD_SECTOR = 0xFFF8,
    FAT_END_OF_CHAIN
    = 0xFFF8, // Check if value is below this to know if another cluster exists
};

enum FAT_ATTRIB {
    FAT_READ_ONLY = 0x01,
    FAT_HIDDEN = 0x02,
    FAT_SYSTEM = 0x04,
    FAT_VOLUME_ID = 0x08,
    FAT_DIRECTORY = 0x10,
    FAT_ARCHIVE = 0x20
};

// TODO: put uint8_t types in the structs
struct fat_extBS_32 {
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

} __attribute__((packed));

struct fat_extBS_16 {
    // extended fat12 and fat16 stuff
    unsigned char bios_drive_num;
    unsigned char reserved1;
    unsigned char boot_signature;
    unsigned int volume_id;
    unsigned char volume_label[11];
    unsigned char fat_type_label[8];

} __attribute__((packed));

struct fat_BS {
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

    // this will be cast to it's specific type once the driver actually knows
    // what type of FAT this is.
    unsigned char extended_section[54];

} __attribute__((packed));

struct fat_filetable {
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
} __attribute__((packed));

// TODO: Resize these fields
// TODO: rename to something like fat_metadata
struct fat_fs {
    uint32_t lba_start;
    uint16_t total_sectors;
    uint16_t sector_size;
    uint16_t cluster_size;
    uint16_t fat_size;
    uint16_t root_dir_sectors;
    uint16_t first_root_dir_sector;
    uint16_t first_data_sector;
    uint16_t data_sectors;
    uint16_t first_fat_sector;
    uint16_t total_clusters;
    uint8_t fat_type;
    sATADevice* device;
    struct fat_BS* bs;
};

struct fat_inode_info {
    struct fat_fs* fat;       // Pointer to fat data
    uint8_t fat_variant;      // FAT32, Fat16, Fat12
    uint32_t init_cluster;    // Initial cluster of chain
    uint32_t current_cluster; // Current cluster being read
    int16_t chain_len;        // Number of clusters in chain, -1 if unknown
    uint32_t dir_cluster;     // Directory cluster of the parent (0 for root)
    uint32_t dir_offset;      // Directory entry offset NOTE: May remove idk
    uint8_t fat_attrib;       // Attributes in fat entry
};

static inline struct fat_fs* dentry_get_fat_fs(struct vfs_dentry* d)
{
    return d->fs_data;
}

int fat_open_file(const inode_t* inode, char* buffer, size_t buffer_size);
void fat_close_file(void* file_start);

struct vfs_dentry* fat_lookup(struct vfs_inode* dir_inode,
                              struct vfs_dentry* child);

void fat_init();

struct vfs_superblock* fat16_mount(sATADevice* device, uint32_t lba_start,
                                   int flags);
struct vfs_inode* fat16_get_root_inode(struct vfs_superblock* sb);

int fat16_read_boot_sector(sATADevice* device, uint32_t lba_start,
                           struct fat_BS* bs);

void fat_fill_meta(struct fat_BS* bs, struct fat_fs* fs);
