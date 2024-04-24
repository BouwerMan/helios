#pragma once
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

// TODO: Support longer file names, only limiting since we only support FAT16 no LFN
typedef struct directory {
    uint8_t mount;
    const char* path;
    const char filename[8];
    const char file_extension[3];
} dir_t;

typedef struct {
    void* file_ptr;
    unsigned char* read_ptr;
    size_t file_size;
} FILE;

void vfs_init(uint8_t maximum_mounts);
bool register_fs(uint8_t fs);
FILE* vfs_open(dir_t directory);
void vfs_close(FILE* file);
