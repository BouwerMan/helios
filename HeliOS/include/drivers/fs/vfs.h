#pragma once
#include <drivers/ata/controller.h>
#include <drivers/ata/device.h>
#include <drivers/ata/partition.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// TODO: Add more filesystems once drivers for them are supported
enum FILESYSTEMS {
    UNSUPPORTED = 0,
    FAT16,
    FAT32, // Not sure these are techincally supported
    FAT12, // Not sure these are techincally supported
};

enum FILETYPE { FILETYPE_FILE, FILETYPE_DIR };

// TODO: Support longer file names, only limiting since we only support FAT16 no
// LFN
typedef struct directory_s {
    uint8_t mount_id; // id of mount point
    const char* path;
    char file[13];          // Entire FAT 8.3 name
    char filename[9];       // extended out so null chars fit
    char file_extension[4]; // extended out so null chars fit
} dir_t;

typedef struct filesystem_s filesystem_t;

struct mount {
    bool present;
    uint8_t id;
    sATADevice* device;
    sPartition* partition;
    filesystem_t* filesystem;
};
typedef struct mount mount_t;

// NOTE: This is not the same as linux inode because I am stupid and don't
// understand it. I am just stealing the name.
// TODO: I need to store sector information or some way for the fs to find the
// file immediately.
typedef struct inode_s {
    int id;                // inode id in cache
    mount_t* mount;        // Top level device information
    char file[13];         // 8.3 Filename
    dir_t* dir;            // File location and name
    uint32_t init_sector;  // Initial sector of the filesystem
    size_t f_size;         // File size
    uint32_t init_cluster; // initial fat cluster, later this should be placed
                           // in fat specific place
    uint8_t loc_type;      // fat location type, either 0 -> ROOT or 1 -> DATA
    void* fs_data;         // Data to fs specific information
} inode_t;

typedef struct {
    void* file_ptr;
    unsigned char* read_ptr;
    size_t file_size;
} FILE;

// TODO: flesh out arguments
typedef int (*f_read)(const inode_t* inode, char* buffer, size_t buffer_size);
typedef void (*f_init)(sATADevice* device, uint32_t lba_start);

struct filesystem_s {
    uint8_t id;
    uint8_t fs_type;
    f_init fs_init;
    f_read read_handler; // Reads inode
    int (*find_inode)(inode_t* inode);
};

// Working on new vfs stuff:

// TODO: Add timestamp stuff
struct vfs_inode {
    int id;
    uint8_t filetype;
    size_t f_size;
    int ref_count;
    uint16_t permissions;
    uint8_t flags;
    void* fs_data;
};

struct vfs_dentry {
    char* name;
    struct vfs_inode* inode;
    struct vfs_dentry* parent; // Reference to parent's directory
    void* fs_data;             // Filesystem specific data
    int ref_count;
    int flags;
};

struct vfs_fs_type {
    char name[8];                                                                       // Filesystem name
    struct vfs_superblock* (*mount)(sATADevice* device, uint32_t lba_start, int flags); // Mount function
    struct vfs_fs_type* next;
};

struct vfs_superblock {
    struct vfs_dentry* root_dentry;
    struct vfs_fs_type* fs_type;
    void* fs_data;
    char* moint_point;
};

void register_filesystem(struct vfs_fs_type* fs);

void vfs_init(uint8_t maximum_filesystems, uint8_t maximum_mounts, size_t inode_cache_size);
bool register_fs(uint8_t fs);
FILE* vfs_open(dir_t* directory);
void vfs_close(FILE* file);
int mount(uint8_t id, sATADevice* device, sPartition* partition, uint8_t fs_type);
