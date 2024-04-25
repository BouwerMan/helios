// https://wiki.osdev.org/FAT#Reading_the_Boot_Sector
#include <kernel/ata/ata.h>
#include <kernel/ata/controller.h>
#include <kernel/ata/device.h>
#include <kernel/fs/fat.h>
#include <kernel/fs/vfs.h>
#include <kernel/liballoc.h>
#include <stdio.h>
#include <string.h>

enum {
    DIRECTORY_TYPE,
    FILE_TYPE,
};

static fat_BS_t* fat_boot;
// TODO: setup proper storage of filesystems
static struct fat_fs* fat;

static void list_directory(struct fat_fs* fs, fat_filetable_t* tables, size_t tables_size, size_t* num_tables);
static void* fat_open_cluster(
    const struct fat_fs* fs, uint16_t* buffer, uint16_t buffer_size, uint32_t cluster, uint16_t sector);
static int fat_open_sector(
    const struct fat_fs* fs, uint16_t* buffer, uint16_t buffer_size, uint32_t sector, uint8_t type);

// TODO: make a struct for all the useful data (FAT_FS esc thing).
//       return success or failure bool
void init_fat(sATADevice* device, uint32_t lba_start)
{
    uint16_t buffer[256] = { 0 };
    printf("Attempting to read device %d\n", device->id);
    bool res = device->rw_handler(device, OP_READ, buffer, lba_start, device->sec_size, 1);
    if (!res) {
        printf("Failed to read device %d\n", device->id);
        return;
    }
    printf("Read success\n");
    fat_boot = kmalloc(sizeof(fat_BS_t));
    fat = kmalloc(sizeof(struct fat_fs));
    memcpy(fat_boot, buffer, sizeof(fat_BS_t));

    fat->total_sectors = (fat_boot->total_sectors_16 == 0) ? fat_boot->total_sectors_32 : fat_boot->total_sectors_16;
    printf("total sectors: %d\n", fat->total_sectors);
    // int fat_size = (fat_boot->table_size_16 == 0)? fat_boot_ext_32->table_size_16 :
    // fat_boot->table_size_16;
    fat->fat_size = fat_boot->table_size_16;
    printf("Fat size: %d\n", fat->fat_size);
    fat->sector_size = fat_boot->bytes_per_sector;
    printf("Sector size: %d\n", fat->sector_size);
    fat->root_dir_sectors
        = ((fat_boot->root_entry_count * 32) + (fat_boot->bytes_per_sector - 1)) / fat_boot->bytes_per_sector;

    fat->data_sectors = fat->total_sectors
        - (fat_boot->reserved_sector_count + (fat_boot->table_count * fat->fat_size) + fat->root_dir_sectors);

    fat->total_clusters = fat->data_sectors / fat_boot->sectors_per_cluster;

    // TODO: Support ExFAT
    if (fat->total_clusters < 4085) {
        fat->fat_type = FAT12;
    } else if (fat->total_clusters < 65525) {
        fat->fat_type = FAT16;
    } else {
        fat->fat_type = FAT32;
    }

    printf("FAT Type: %d\n", fat->fat_type);

    // NOTE: I know I am using FAT16 now so I am just going to tailor for that while testing
    //       Most everything below this should be moved to an fopen function

    fat->first_data_sector
        = fat_boot->reserved_sector_count + (fat_boot->table_count * fat->fat_size) + fat->root_dir_sectors;
    printf("first_data_sector: %d\n", fat->first_data_sector);
    fat->first_root_dir_sector = fat->first_data_sector - fat->root_dir_sectors;
    printf("first_root_dir_sector: %d\n", fat->first_root_dir_sector);

    printf("secperclust: %d\n", fat_boot->sectors_per_cluster);
    fat->lba_start = lba_start;
    fat->device = device;
    // int cluster = 2;
    //
    // int first_sector_of_cluster
    //     = ((cluster - 2) * fat_boot->sectors_per_cluster) + first_data_sector;

    // NOTE: I think this translates properly, if i understand, multiple sectors make up a cluster,
    // since this is the first one i can just use the sector and i'll increment from there.
    return;
    int active_cluster = 2;
    bool next = true;
    while (next) {
        uint8_t fat_table[fat->sector_size];
        uint16_t fat_offset = active_cluster * 2;
        // NOTE: Not sure first_root_dir_sector is right here, might have to manually find first
        // sector of active_cluster
        uint16_t fat_sector = fat->first_root_dir_sector + (fat_offset / fat->sector_size);
        uint16_t ent_offset = fat_offset % fat->sector_size;
        memset(buffer, 0, sizeof(uint16_t) * 256);
        printf("Reading sector: %d, cluster: %d\n", fat_sector, active_cluster);
        if (!device->rw_handler(device, OP_READ, (uint16_t*)buffer, 63 + fat_sector, device->sec_size, 1)) {
            printf("Could not read from disk\n");
            return;
        }
        memcpy(fat_table, (uint8_t*)buffer, fat->sector_size);
        unsigned short table_value = *(unsigned short*)&fat_table[ent_offset];
        // for (size_t i = 64; i < sector_size; i++) {
        //     printf("0x%X ", fat_table[i]);
        // }
        // return;
        printf("\ntable_value: %d\n", table_value);
        for (size_t i = 0; i < fat->sector_size; i += 64) {
            if (fat_table[i] == 0x0) {
                puts("No more files");
                next = false;
                break;
            } else if (fat_table[i] == 0xE5) {
                // TODO: Unused entry
            }
            if (fat_table[i + 10] == 0x0F) {
                puts("long file name entry");
            } else {
                puts("not long file name");
                printf("i: %d\n", i);
                for (size_t j = i; j < 11 + i; j++) {
                    printf("%c", fat_table[j]);
                }
                puts("");
            }
        }

        const int to_check = 64;
        // for (int k = to_check; k < to_check + 32; k++) {
        //     printf("i: %d, 0x%X ", k, fat_table[k]);
        // }
        // puts("");

        // Manually read file test.txt

        uint16_t cluster = ((fat_table[to_check + 27] << 8) | (fat_table[to_check + 26])) - 2;
        printf("test cluster: %d\n", cluster);
        printf("secperclust: %d\n", fat_boot->sectors_per_cluster);

        uint16_t fat_offset2 = cluster * fat_boot->sectors_per_cluster;
        uint16_t fat_sector2 = fat->first_data_sector + fat_offset2;
        memset(buffer, 0, sizeof(uint16_t) * 256);
        printf("Fat offset: %d. Reading sector: %d\n", fat_offset2, fat_sector2 + 63);
        if (!device->rw_handler(device, OP_READ, (uint16_t*)buffer, fat_sector2 + 63, device->sec_size, 1)) {
            printf("Could not read from disk\n");
            return;
        }

        uint8_t* tmp = (uint8_t*)buffer;
        uint32_t fsize = 0;
        for (size_t i = 0; i < 4; i++) {
            fsize += fat_table[32 - i] << ((4 - i) * 8);
        }
        for (int i = 0; i < fsize; i++) {
            if (tmp[i]) printf("%c", tmp[i]);
        }
        puts("");

        if (table_value >= 0xFFF8) {
            puts("No more clusters in chain");
            next = false;
        } else if (table_value == 0xFFF7) {
            puts("Sector marked as bad");
            next = false;
        } else {
            active_cluster = table_value;
            // active_cluster++;
            // next = false;
        }
    }
}

/// Max number of files to check, eventually should be infinite but i don't trust myself yet.
const static uint8_t MAX_FILES = 16;

// TODO: Ignores file extensions
int fat_open_file(const inode_t* inode, char* buffer, size_t buffer_size)
{
    // TODO: Support directories
    // TODO: Account for filesize

    return fat_open_sector(fat, (uint16_t*)buffer, buffer_size, inode->init_sector, FILE_TYPE);
}

void fat_close_file(void* file_start) { kfree(file_start); }

/// Finds inode and fills out remaining inode parameters.
/// NOTE: Requires dir and mount to be filled in so it can know where to look.
/// TODO: Maybe rework that?
int fat_find_inode(inode_t* inode)
{
    inode->f_size = 0;
    fat_filetable_t* file_tables = kmalloc(sizeof(fat_filetable_t) * MAX_FILES);
    size_t num_tables = 0;
    list_directory(fat, file_tables, MAX_FILES, &num_tables);

    for (size_t i = 0; i < num_tables; i++) {
        // Check if name matches
        if (strncmp(inode->dir->filename, file_tables[i].name, 8)) continue;
        if (strncmp(inode->dir->file_extension, file_tables[i].ext, 3)) continue;
        // TODO: Add more checks, too lazy rn so it just checks file name and calls it quits

        // Now we just fill out the inode
        inode->f_size = file_tables[i].size;
        // NOTE: Does not correctly offset based on root dir or data sectors yet.
        // Planning on doing that in fat_open_sector()
        inode->init_sector
            = inode->mount->partition->start + (file_tables[i].cluster - 2) * fat_boot->sectors_per_cluster;
        break;
    }

    kfree(file_tables);
    if (inode->f_size)
        return 0;
    else
        return 1;
}

// TODO: Currently only supports reading from root_dir
static void list_directory(struct fat_fs* fs, fat_filetable_t* tables, size_t tables_size, size_t* num_tables)
{
    uint8_t fat_table[fs->sector_size];
    // Controls which directory we read
    uint8_t active_cluster = 2;
    fat_open_cluster(fs, (uint16_t*)fat_table, fs->sector_size / 2, active_cluster, fs->first_root_dir_sector);

    for (size_t i = 0; i < fs->sector_size; i += 64) {
        if (fat_table[i] == 0x0) {
            break;
        } else if (fat_table[i] == 0xE5) {
            // TODO: Unused entry
            continue;
        }
        // tables = (fat_filetable_t)fat_table[i];
        if (fat_table[i + 10] == 0x0F) {
            puts("long file name entry not supported");
            continue;
        } else {
            memcpy(tables, fat_table + i, 32);
            for (uint8_t j = 0; j < 8; j++) {
                if (tables->name[j] == ' ') tables->name[j] = '\0';
                if (j < 3) {
                    if (tables->ext[j] == ' ') tables->ext[j] = '\0';
                }
            }
            tables++;
            (*num_tables)++;
        }
    }
    return;
}

static int fat_open_sector(
    const struct fat_fs* fs, uint16_t* buffer, uint16_t buffer_size, uint32_t sector, uint8_t type)
{
    uint32_t offset = 0;
    if (type == DIRECTORY_TYPE) {
        offset = fs->first_root_dir_sector;
    } else if (type == FILE_TYPE) {
        offset = fs->first_data_sector;
    }
    uint16_t read_buff[256] = { 0 };
    printf("Reading from sector: %d\n", sector + offset);
    if (!fs->device->rw_handler(fs->device, OP_READ, read_buff, sector + offset, fs->device->sec_size, 1)) {
        printf("Could not read from disk\n");
        return -1;
    }
    // Copy into caller's buffer, manually making sure we don't read past end of read_buff
    if (buffer_size <= 256) {
        memcpy(buffer, read_buff, buffer_size);
    } else {
        memcpy(buffer, read_buff, 256);
    }
    return 0;
}

// sector should be either first_root_dir_sector or first_data_sector, specifys where to look for sector i think
static void* fat_open_cluster(
    const struct fat_fs* fs, uint16_t* buffer, uint16_t buffer_size, uint32_t cluster, uint16_t sector)
{
    uint16_t read_buff[256] = { 0 };
    uint32_t lba_addr = fs->lba_start + (cluster - 2) * fat_boot->sectors_per_cluster;
    printf("lba_addr: %d\n", lba_addr);
    if (!fs->device->rw_handler(fs->device, OP_READ, read_buff, lba_addr + sector, fs->device->sec_size, 1)) {
        printf("Could not read from disk\n");
        return NULL;
    }
    // Copy into caller's buffer, manually making sure we don't read past end of read_buff
    if (buffer_size <= 256) {
        return memcpy(buffer, read_buff, buffer_size);
    } else {
        return memcpy(buffer, read_buff, 256);
    }
}
