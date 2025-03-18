// https://wiki.osdev.org/FAT#Reading_the_Boot_Sector
#include <drivers/ata/ata.h>
#include <drivers/ata/controller.h>
#include <drivers/ata/device.h>
#include <drivers/fs/fat.h>
#include <drivers/fs/vfs.h>
#include <kernel/liballoc.h>
#include <kernel/sys.h>
#include <stdio.h>
#include <string.h>

// https://unix.stackexchange.com/questions/209566/how-to-format-a-partition-inside-of-an-img-file

enum {
    DIRECTORY_TYPE,
    FILE_TYPE,
};
enum LOC_TYPE { ROOT = 0, DATA = 1 };

static fat_BS_t* fat_boot;
// TODO: setup proper storage of filesystems
static struct fat_fs* fat;

static void list_directory(struct fat_fs* fs, fat_filetable_t* tables,
    size_t tables_size, size_t* num_tables);
static void* fat_open_cluster(const struct fat_fs* fs, uint16_t* buffer,
    uint16_t buffer_size, uint32_t cluster, uint16_t sector);
static int fat_open_sector(const struct fat_fs* fs, uint8_t* buffer,
    uint16_t buffer_size, uint32_t sector);
static int fat_open_clust(const struct fat_fs* fs, uint8_t* buffer,
    size_t buffer_size, uint32_t cluster, enum LOC_TYPE type);
static uint32_t fat_get_next_cluster(
    const struct fat_fs* fs, uint32_t first_cluster);
static void parse_directory(struct fat_fs* fs, fat_filetable_t* tables,
    size_t max_tables, size_t* num_tables, uint8_t* fat_table,
    size_t table_size);

// TODO: make a struct for all the useful data (FAT_FS esc thing).
//       return success or failure bool
void init_fat(sATADevice* device, uint32_t lba_start)
{
    uint16_t buffer[256] = { 0 };
    printf("Attempting to read device %d\n", device->id);
    bool res = device->rw_handler(
        device, OP_READ, buffer, lba_start, device->sec_size, 1);
    if (!res) {
        printf("Failed to read device %d\n", device->id);
        return;
    }
    printf("Read success\n");
    fat_boot = kmalloc(sizeof(fat_BS_t));
    fat = kmalloc(sizeof(struct fat_fs));
    memcpy(fat_boot, buffer, sizeof(fat_BS_t));

    fat->total_sectors = (fat_boot->total_sectors_16 == 0)
        ? fat_boot->total_sectors_32
        : fat_boot->total_sectors_16;
    printf("total sectors: %d\n", fat->total_sectors);
    // int fat_size = (fat_boot->table_size_16 == 0)?
    // fat_boot_ext_32->table_size_16 : fat_boot->table_size_16;
    fat->fat_size = fat_boot->table_size_16;
    printf("Fat size: %d\n", fat->fat_size);
    fat->sector_size = fat_boot->bytes_per_sector;
    printf("Sector size: %d\n", fat->sector_size);
    fat->cluster_size = fat->sector_size
        * fat_boot->sectors_per_cluster; // number of bytes in a cluster
    printf("Cluster size: %d\n", fat->cluster_size);

    fat->root_dir_sectors
        = ((fat_boot->root_entry_count * 32) + (fat_boot->bytes_per_sector - 1))
        / fat_boot->bytes_per_sector;

    fat->data_sectors = fat->total_sectors
        - (fat_boot->reserved_sector_count
            + (fat_boot->table_count * fat->fat_size) + fat->root_dir_sectors);

    fat->total_clusters = fat->data_sectors / fat_boot->sectors_per_cluster;
    printf("Total Clusters: %d\n", fat->total_clusters);

    // TODO: Support ExFAT
    if (fat->total_clusters < 4085) {
        fat->fat_type = FAT12;
        // panic("We don't like FAT12");
    } else if (fat->total_clusters < 65525) {
        fat->fat_type = FAT16;
    } else {
        // panic("We don't like FAT32");
        fat->fat_type = FAT32;
    }

    printf("FAT Type: %d\n", fat->fat_type);

    // NOTE: I know I am using FAT16 now so I am just going to tailor for that
    // while testing
    //       Most everything below this should be moved to an fopen function

    fat->first_fat_sector = fat_boot->reserved_sector_count;
    printf("first_fat_sector: %d\n", fat->first_fat_sector);
    fat->first_data_sector = fat_boot->reserved_sector_count
        + (fat_boot->table_count * fat->fat_size) + fat->root_dir_sectors;
    printf("first_data_sector: %d\n", fat->first_data_sector);
    fat->first_root_dir_sector = fat->first_data_sector - fat->root_dir_sectors;
    printf("first_root_dir_sector: %d\n", fat->first_root_dir_sector);

    printf("secperclust: %d\n", fat_boot->sectors_per_cluster);
    fat->lba_start = lba_start;
    fat->device = device;
    // int cluster = 2;
    //
    // int first_sector_of_cluster
    //     = ((cluster - 2) * fat_boot->sectors_per_cluster) +
    //     first_data_sector;

    // NOTE: I think this translates properly, if i understand, multiple sectors
    // make up a cluster, since this is the first one i can just use the sector
    // and i'll increment from there. asm volatile("1: jmp 1b");

    return;
#if 0
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
#endif
}

/// Max number of files to check, eventually should be infinite but i don't
/// trust myself yet.
const static uint8_t MAX_FILES = 16;

// TODO: Ignores file extensions
int fat_open_file(const inode_t* inode, char* buffer, size_t buffer_size)
{
    // TODO: Support directories
    // TODO: Account for filesize

    uint32_t cluster_size = fat->sector_size
        * fat_boot->sectors_per_cluster; // number of bytes in a cluster
    uint32_t num_clusters
        = (buffer_size / (fat->sector_size * fat_boot->sectors_per_cluster)
            + 1); // number of clusters to read (approx)

    uint32_t next_cluster = inode->init_cluster;
    size_t i = 0;
    while (next_cluster < FAT_END_OF_CHAIN) {
        next_cluster = fat_get_next_cluster(fat, next_cluster);
        fat_open_clust(fat, (uint8_t*)(buffer + (i * cluster_size)),
            buffer_size - (i * cluster_size), next_cluster, DATA);
        i++;
    }
    // NOTE: LETS GOOOOOOOO THIS THING READS THE ENTIRE FILE AT ONCE IM
    // LITERALLY GONNA

    // return fat_open_clust(fat, buffer, buffer_size, inode->init_cluster,
    // DATA);
    return 0;
    // return fat_open_sector(fat, buffer, buffer_size, inode->init_sector +
    // fat->first_data_sector);
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
        if (strncmp(inode->dir->file_extension, file_tables[i].ext, 3))
            continue;
        // TODO: Add more checks, too lazy rn so it just checks file name and
        // calls it quits

        // Now we just fill out the inode
        inode->f_size = file_tables[i].size;
        // NOTE: Does not correctly offset based on root dir or data sectors
        // yet. Planning on doing that in fat_open_sector()
        inode->init_sector = inode->mount->partition->start
            + (file_tables[i].cluster - 2) * fat_boot->sectors_per_cluster;
        inode->init_cluster = file_tables[i].cluster;
        break;
    }

    kfree(file_tables);
    if (inode->f_size)
        return 0;
    else
        return 1;
}

void fat_dir(const char* dir)
{
    printf("Reading directory: %s\n", dir);
    fat_filetable_t* file_tables = kmalloc(sizeof(fat_filetable_t) * MAX_FILES);
    size_t num_tables = 0;
    // First we read the root sector
    uint8_t fat_table[fat->cluster_size];
    uint8_t active_cluster = 2; // for root dir
    uint32_t first_sector = fat->first_root_dir_sector;
    fat_open_clust(fat, fat_table, fat->cluster_size, 2, ROOT);
    parse_directory(
        fat, file_tables, MAX_FILES, &num_tables, fat_table, fat->cluster_size);
    // list_directory(fat, file_tables, MAX_FILES, &num_tables);

    // Testing loop to view tables
    for (int i = 0; i < num_tables; ++i) {
        puts(file_tables[i].name);
        printf("file cluster: %d\n", file_tables[i].cluster);
    }

    // If the directory is more than just the root dir, check futher down
    if (dir[1] != '\0') {
        fat_filetable_t* subdir_files
            = kmalloc(sizeof(fat_filetable_t) * MAX_FILES);
        // Get locations for each '/'
        // TODO: Should make an strtok or similar function in libc
        int split[16] = { 0 };
        size_t tokens = 0;
        for (int i = 0; i < 16; i++) {
            putchar(dir[i]);
            if ((dir[i] == '/') || (dir[i] == '\0')) split[tokens++] = i;
            if (dir[i] == '\0') break;
        }
        for (int i = 0; i < 16; i++)
            printf(" %d", split[i]);
        // Basically iterates through each token in split[]
        for (size_t i = 0; i < tokens; i++) {
            for (size_t table = 0; table < num_tables; table++) {
                const char* token_start = dir + split[i] + 1;
                int token_len = dir + split[i + 1] - token_start;
                if (token_len < 0) break;
                char token[16] = { 0 };
                memcpy(token, token_start, token_len);
                printf("Token: %s, token length: %d\n", token, token_len);
                if (strncmp(file_tables[table].name, token_start, token_len))
                    continue;
                if (!(file_tables[table].attrib & FAT_DIRECTORY)) continue;
                // now we found the directory

                printf("Found subdirectory %s\n", file_tables[table].name);
                active_cluster = file_tables[table].cluster;
                num_tables = 0;
                fat_open_clust(
                    fat, fat_table, fat->cluster_size, active_cluster, DATA);
                parse_directory(fat, subdir_files, MAX_FILES, &num_tables,
                    fat_table, fat->cluster_size);

                // Testing loop to view tables
                for (int i = 0; i < num_tables; ++i) {
                    printf("file name: %s, file size: %d, file cluster: %d\n",
                        subdir_files[i].name, subdir_files[i].size,
                        subdir_files[i].cluster);
                }
                break;
            }
            kfree(file_tables);
            file_tables = subdir_files;
        }
        kfree(subdir_files);
    }

    // asm volatile("1: jmp 1b");
    kfree(file_tables);
    return;
}

// TODO: Currently only supports reading from root_dir
static void list_directory(struct fat_fs* fs, fat_filetable_t* tables,
    size_t tables_size, size_t* num_tables)
{
    uint8_t fat_table[fs->sector_size];
    // Controls which directory we read
    uint8_t active_cluster = 2;
    fat_open_cluster(fs, (uint16_t*)fat_table, fs->sector_size / 2,
        active_cluster, fs->first_root_dir_sector);

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

/// Fills filetables, num_tables becomes number of found files
/// Expects entire fat_table (cluster)
static void parse_directory(struct fat_fs* fs, fat_filetable_t* tables,
    size_t max_tables, size_t* num_tables, uint8_t* fat_table,
    size_t table_size)
{
    for (size_t i = 0; i < table_size; i += 32) {
        if (fat_table[i] == 0x0) {
            break;
        } else if (fat_table[i] == 0xE5) {
            // TODO: Unused entry
            puts("Unused entry");
            continue;
        }
        // tables = (fat_filetable_t)fat_table[i];
        if (fat_table[i + 11] == 0x0F) {
            puts("long file name entry not supported");
            // i += 32;
            // NOTE: Might need this depending on how LFN actually
            // works idk I havent had any issues yet.
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
}

/**
 * @brief gets next cluster in chain
 * @param[in] first_cluster First cluster of the file (DOES THIS NEED TO BE
 * PREVIOUS CLUSTER INSTEAD)
 *
 * @returns cluster
 */
static uint32_t fat_get_next_cluster(
    const struct fat_fs* fs, uint32_t first_cluster)
{
    // TODO: document basic blurb on how this all works

    // Setting up offsets
    uint32_t active_cluster = first_cluster;
    unsigned char FAT_table[fs->sector_size];
    unsigned int fat_offset = active_cluster * 2;
    // Have to add lba_start, TODO: make lba start included in first_fat sector?
    unsigned int fat_sector
        = fs->first_fat_sector + (fat_offset / fs->sector_size) + fs->lba_start;
    unsigned int ent_offset = fat_offset % fs->sector_size;

    // at this point you need to read from sector "fat_sector" on the disk into
    // "FAT_table".
    //  TODO: Handle errors
    fat_open_sector(fs, FAT_table, fs->sector_size, fat_sector); // First sector

    unsigned short table_value = *(unsigned short*)&FAT_table[ent_offset];

    // the variable "table_value" now has the information you need about the
    // next cluster in the chain.

    if (table_value > 0xFFF8)
        puts("Last cluster in string");
    else if (table_value == 0xFFF8)
        puts("Cluster marked as bad");

    return table_value;
}

// NOTE: Doing things kinda funky, this is made to replace the old
// fat_open_cluster() This function SHOULD read the entire cluster to memory
// type refers to either root directory or data directory sectors
static int fat_open_clust(const struct fat_fs* fs, uint8_t* buffer,
    size_t buffer_size, uint32_t cluster, enum LOC_TYPE type)
{
    // TODO: Put sectors per cluster in fat_fs

    uint32_t sector = type ? fs->first_data_sector : fs->first_root_dir_sector;
    uint32_t lba_addr = fs->lba_start + sector
        + ((cluster - 2) * fat_boot->sectors_per_cluster);
    uint32_t cluster_size = fs->sector_size * fat_boot->sectors_per_cluster;
    // Holds entire cluster
    uint8_t* cluster_data = kmalloc(cluster_size);
    printf("cluster: %d, lba_addr: %d, type: %d\n", cluster, lba_addr, type);
    for (size_t i = 0; i < fat_boot->sectors_per_cluster; i++) {
        uint8_t* tmp = cluster_data + (i * fs->sector_size);
        fat_open_sector(fs, tmp, fat->sector_size, lba_addr + i);
        // asm volatile("1: jmp 1b");
    }
    // asm volatile("1: jmp 1b");
    // puts(cluster_data);
    // Copy into caller's buffer, manually making sure we don't read past end of
    // read_buff
    if (buffer_size <= cluster_size) {
        memcpy(buffer, cluster_data, buffer_size);
    } else {
        memcpy(buffer, cluster_data, cluster_size);
    }
    kfree(cluster_data);
    return 0;
}

// TODO: Do we want to integrate this into other functions? This feels kinda
// useless and basically just a wrapper for the rw_handler.
/**
 *  @brief Modifies input buffer with sector data from disk. This function will
 * handle casting for device drivers.
 *
 *  @param[in] fs Pointer to fat filesystem.
 *  @param[in] buffer Byte array to place data.
 *  @param[in] buffer_size Size of buffer in bytes.
 *  @param[in] sector Logical sector to read.
 *  @returns 0 if success, 1 if failure.
 */
static int fat_open_sector(const struct fat_fs* fs, uint8_t* buffer,
    uint16_t buffer_size, uint32_t sector)
{
    uint16_t read_buff[256] = { 0 };
    // printf("Reading from sector: %d\n", sector);
    if (!fs->device->rw_handler(
            fs->device, OP_READ, read_buff, sector, fs->device->sec_size, 1)) {
        printf("Could not read from disk\n");
        return -1;
    }
    // Copy into caller's buffer, manually making sure we don't read past end of
    // read_buff
    if (buffer_size <= 512) {
        memcpy(buffer, read_buff, buffer_size);
    } else {
        memcpy(buffer, read_buff, 512);
    }
    return 0;
}

// sector should be either first_root_dir_sector or first_data_sector, specifys
// where to look for sector i think
static void* fat_open_cluster(const struct fat_fs* fs, uint16_t* buffer,
    uint16_t buffer_size, uint32_t cluster, uint16_t sector)
{
    // 256 because it is 16 bits so 2 bytes (equals full 512 bytes per sector)
    uint16_t read_buff[256] = { 0 };
    uint32_t lba_addr = fs->lba_start + sector
        + (cluster - 2) * fat_boot->sectors_per_cluster;
    printf("lba_addr: %d\n", lba_addr);
    if (!fs->device->rw_handler(fs->device, OP_READ, read_buff, lba_addr,
            fs->device->sec_size, 1)) {
        printf("Could not read from disk\n");
        return NULL;
    }
    // Copy into caller's buffer, manually making sure we don't read past end of
    // read_buff
    if (buffer_size <= 256) {
        return memcpy(buffer, read_buff, buffer_size);
    } else {
        return memcpy(buffer, read_buff, 512);
    }
}
