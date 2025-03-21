// https://wiki.osdev.org/FAT#Reading_the_Boot_Sector
#include <ctype.h>
#include <drivers/ata/ata.h>
#include <drivers/ata/controller.h>
#include <drivers/ata/device.h>
#include <drivers/fs/fat.h>
#include <drivers/fs/vfs.h>
#include <kernel/liballoc.h>
#include <kernel/sys.h>
#include <kernel/timer.h>
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

static int fat_open_sector(const struct fat_fs* fs, uint8_t* buffer, uint16_t buffer_size, uint32_t sector);
static int fat_open_cluster(const struct fat_fs* fs, uint8_t* buffer, size_t buffer_size, uint32_t cluster);
static uint32_t fat_get_next_cluster(const struct fat_fs* fs, uint32_t prev_cluster);
static void parse_directory(struct fat_fs* fs, fat_filetable_t* tables, size_t max_tables, size_t* num_tables,
                            uint8_t* fat_table, size_t table_size);

static uint8_t* fat_read_root_dir(struct fat_fs* fat, size_t* root_size);
static int fat_scan_dir(mount_t* mount, uint32_t start_cluster, int (*callback)(const void* entry, void* context),
                        void* context);
static int fat_process_dir_entries(uint8_t* dir_data, size_t dir_size,
                                   int (*callback)(const void* entry, void* context), void* context);

struct vfs_fs_type fat16_fs_type = { .name = "fat16", .mount = fat16_mount, .next = NULL };

void fat_init() { register_filesystem(&fat16_fs_type); }

struct vfs_superblock* fat16_mount(sATADevice* device, uint32_t lba_start, int flags)
{
    struct vfs_superblock* sb = (struct vfs_superblock*)kmalloc(sizeof(struct vfs_superblock));
    if (!sb) return NULL;

    fat_BS_t* bs = (fat_BS_t*)kmalloc(sizeof(fat_BS_t));
    if (!bs) return NULL;
    int res = fat16_read_boot_sector(device, lba_start, bs);
    if (res < 0) return NULL;

    struct fat_fs* fs = (struct fat_fs*)kmalloc(sizeof(struct fat_fs));
    if (!fs) return NULL;
    fat16_fill_meta(bs, fs);

    sb->fs_type = &fat16_fs_type;
    sb->fs_data = (void*)&fs;

    return NULL;
}

/// Reads boot sector. returns 1 on success, -1 on failure.
int fat16_read_boot_sector(sATADevice* device, uint32_t lba_start, fat_BS_t* bs)
{
    uint16_t buffer[256] = { 0 };
    printf("Attempting to read device %d\n", device->id);
    bool res = device->rw_handler(device, OP_READ, buffer, lba_start, device->sec_size, 1);
    if (!res) {
        printf("Failed to read device %d\n", device->id);
        return -1;
    }
    memcpy(bs, buffer, sizeof(fat_BS_t));
    return 1;
}

// Calculates important offsets and such and
int fat16_fill_meta(fat_BS_t* bs, struct fat_fs* fs) { }

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
    // int fat_size = (fat_boot->table_size_16 == 0)?
    // fat_boot_ext_32->table_size_16 : fat_boot->table_size_16;
    fat->fat_size = fat_boot->table_size_16;
    printf("Fat size: %d\n", fat->fat_size);
    fat->sector_size = fat_boot->bytes_per_sector;
    printf("Sector size: %d\n", fat->sector_size);
    fat->cluster_size = fat->sector_size * fat_boot->sectors_per_cluster; // number of bytes in a cluster
    printf("Cluster size: %d\n", fat->cluster_size);

    fat->root_dir_sectors
        = ((fat_boot->root_entry_count * 32) + (fat_boot->bytes_per_sector - 1)) / fat_boot->bytes_per_sector;

    fat->data_sectors = fat->total_sectors
        - (fat_boot->reserved_sector_count + (fat_boot->table_count * fat->fat_size) + fat->root_dir_sectors);

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
    fat->first_data_sector
        = fat_boot->reserved_sector_count + (fat_boot->table_count * fat->fat_size) + fat->root_dir_sectors;
    printf("first_data_sector: %d\n", fat->first_data_sector);
    fat->first_root_dir_sector = fat->first_data_sector - fat->root_dir_sectors;
    printf("first_root_dir_sector: %d\n", fat->first_root_dir_sector);

    printf("secperclust: %d\n", fat_boot->sectors_per_cluster);
    fat->lba_start = lba_start;
    fat->device = device;

    return;
}

/// Max number of files to check, eventually should be infinite but i don't
/// trust myself yet.
const static uint8_t MAX_FILES = 16;

// TODO: Ignores file extensions
// TODO: Support directories
// TODO: Account for filesize
int fat_open_file(const inode_t* inode, char* buffer, size_t buffer_size)
{
    uint32_t cluster_size = fat->sector_size * fat_boot->sectors_per_cluster; // number of bytes in a cluster
    uint32_t num_clusters
        = (buffer_size / (fat->sector_size * fat_boot->sectors_per_cluster) + 1); // number of clusters to read (approx)

    uint32_t next_cluster = inode->init_cluster;
    size_t i = 0;
    while (next_cluster < FAT_END_OF_CHAIN) {
        next_cluster = fat_get_next_cluster(fat, next_cluster);
        fat_open_cluster(fat, (uint8_t*)(buffer + (i * cluster_size)), buffer_size - (i * cluster_size), next_cluster);
        i++;
    }

    return 0;
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
    // list_directory(fat, file_tables, MAX_FILES, &num_tables);
    uint8_t fat_table[fat->cluster_size];
    uint8_t active_cluster = 2; // for root dir
    uint32_t first_sector = fat->first_root_dir_sector;
    fat_open_cluster(fat, fat_table, fat->cluster_size, 0);
    parse_directory(fat, file_tables, MAX_FILES, &num_tables, fat_table, fat->cluster_size);

    for (size_t i = 0; i < num_tables; i++) {
        // Check if name matches
        if (strncmp(inode->dir->filename, file_tables[i].name, 8)) continue;
        if (strncmp(inode->dir->file_extension, file_tables[i].ext, 3)) continue;
        // TODO: Add more checks, too lazy rn so it just checks file name and
        // calls it quits

        // Now we just fill out the inode
        inode->f_size = file_tables[i].size;
        // NOTE: Does not correctly offset based on root dir or data sectors
        // yet. Planning on doing that in fat_open_sector()
        inode->init_sector
            = inode->mount->partition->start + (file_tables[i].cluster - 2) * fat_boot->sectors_per_cluster;
        inode->init_cluster = file_tables[i].cluster;
        break;
    }

    kfree(file_tables);
    if (inode->f_size)
        return 0;
    else
        return 1;
}

/**
 * @brief Normalizes a FAT16 filename by extracting, trimming spaces, and
 * converting to lowercase.
 *
 * This function extracts an 8.3 filename from a FAT16 directory entry, removes
 * trailing spaces, converts it to lowercase, and formats it as "NAME.EXT".
 *
 * @param entry Pointer to a 32-byte FAT16 directory entry.
 * @param output Pre-allocated buffer (at least 13 bytes) to store the formatted
 * filename.
 */
static void fat_normalize_filename(const uint8_t* entry, char* output)
{
    // First removing trailing spaces in name_end
    char name[8] = { 0 };
    memcpy(name, entry, 8);
    int name_len = 8;
    for (; name_len > 0; name_len--) {
        if (name[name_len - 1] != ' ') break;
    }
    if (name_len == 0) {
        output[0] = '\0';
        return;
    }
    memcpy(output, name, name_len);

    char ext[3] = { 0 };
    memcpy(ext, entry + 8, 3);
    int ext_len = 3;
    for (; ext_len > 0; ext_len--) {
        if (ext[ext_len - 1] != ' ') break;
    }
    if (ext_len > 0) {
        output[name_len] = '.';
        memcpy(output + name_len + 1, ext, ext_len);
        output[name_len + ext_len + 1] = '\0'; // add null terminator
    } else {
        output[name_len] = '\0';
    }
}

// TODO: Remove
void test_fat_normalize_filename(const char* raw_entry, const char* expected)
{
    uint8_t entry[11] = { 0 };
    char output[13] = { 0 };

    // Copy raw FAT16 entry into test entry buffer
    memcpy(entry, raw_entry, strlen(raw_entry));

    // Normalize the filename
    fat_normalize_filename(entry, output);

    // Print the result
    printf("Raw Entry:   \"%.8s.%.3s\"\n", entry, entry + 8);
    printf("Normalized:  \"%s\"\n", output);
    printf("Expected:    \"%s\"\n", expected);
    printf("Test %s\n\n", strcmp(output, expected) == 0 ? "PASSED" : "FAILED");
}

/**
 * @brief Compares two filenames case-insensitively.
 *
 * This function performs a case-insensitive comparison between a normalized
 * FAT16 filename and a target filename.
 *
 * @param fat_name Normalized FAT16 filename (from `fat_normalize_filename()`).
 * @param target_name Target filename to compare against.
 * @returns true if filenames match, false otherwise.
 */
bool fat_compare_filenames(const char* fat_name, const char* target_name)
{
    size_t name_len = strlen(fat_name);
    size_t target_len = strlen(target_name);
    if (name_len != target_len) return false;
    for (int i = 0; i < name_len && i < target_len; i++) {
        if (fat_name[i] != toupper(target_name[i])) return false;
    }
    return true;
}

/**
 * @brief Callback function for scanning a FAT directory to find a specific
 * file.
 *
 * This function extracts the filename and extension from a 32-byte FAT
 * directory entry and compares it with the target filename stored in the
 * `inode_t` structure. If a match is found, it populates the `inode_t` with
 * metadata and signals `fat_scan_dir()` to stop scanning.
 *
 * @param entry Pointer to a 32-byte FAT directory entry.
 * @param context Pointer to an `inode_t` structure where file
 * metadata will be stored.
 * @returns 1 if the file is found (stop scanning), 0 to continue scanning,
 *          or -1 on error.
 *
 * @note The filename comparison is case-insensitive, and spaces in FAT
 *       filenames are ignored.
 */
int fat_lookup_inode_callback(const void* entry, void* context)
{
    fat_filetable_t file_table;
    inode_t* inode = (inode_t*)context;

    memcpy(&file_table, entry, 32);
    char norm_name[13] = { 0 };
    fat_normalize_filename(entry, norm_name);
    if (fat_compare_filenames(norm_name, inode->file)) {
        // Populate inode with file metadata
        inode->f_size = file_table.size;          // File size (bytes)
        inode->init_cluster = file_table.cluster; // First cluster

        return 1; // Stop scanning, file found
    }
    return 0;
}

// TODO: use mount to find fat_boot and BPB.
// TODO: Currently does not support relative paths.

/**
 * @brief Finds a file or directory in a FAT16 filesystem and populates its
 * inode.
 *
 * This function searches for a file or directory at the given path within the
 * mounted FAT16 filesystem. If found, it populates the provided `inode_t`
 * structure with the file's metadata, including its size and starting cluster.
 *
 * @param path The absolute or relative path to the file or directory.
 * @param mount Pointer to the mounted FAT16 filesystem structure.
 * @param out_inode Pointer to an `inode_t` structure where the file metadata
 *                  will be stored if the lookup is successful.
 * @returns 1 if the file is found and `out_inode` is populated, 0 if file is
 *          not found, -1 if an error occured.
 *
 * @note The search is case-insensitive, and long filenames (LFN) are not
 *       supported.
 */
int fat_lookup_inode(const char* path, const mount_t* mount, inode_t* out_inode)
{
    const size_t path_len = strlen(path);
    char buffer[path_len + 1];
    strncpy(buffer, path, path_len + 1);
    char* token = strtok(buffer, "/");
    inode_t inode = { 0 };
    inode.init_cluster = 0;
    int res = 0;
    // Iterate through tokens, if token == NULL immediately then that just means the path is "/"
    do {
        strcpy(inode.file, token);
        res = fat_scan_dir(mount, inode.init_cluster, fat_lookup_inode_callback, (void*)&inode);
        if (res < 0) {
            return res;
        } else if (res > 0) {
            memcpy(out_inode, &inode, sizeof(inode_t));
        }
        token = strtok(NULL, "/");
    } while (token != NULL);

    return res;
}

// I guess this just prints out directory files
void fat_dir(inode_t* inode)
{
    // printf("Reading directory: %s\n", dir);
}

/**
 * @brief Reads the FAT root directory into a dynamically allocated buffer.
 *
 * This function allocates memory and loads the entire root directory from disk.
 * The caller is responsible for freeing the returned buffer after use.
 *
 * @param fat Pointer to the FAT filesystem structure.
 * @param root_size[out] Calculated root_size (length of buffer returned).
 * @returns Pointer to a dynamically allocated buffer containing the root
 *          directory data, or NULL if memory allocation or disk read fails.
 *
 * @note The caller must free the returned buffer using `kfree()` to prevent
 *       memory leaks.
 */
static uint8_t* fat_read_root_dir(struct fat_fs* fat, size_t* root_size)
{
    *root_size = fat->root_dir_sectors * fat->sector_size;
    uint8_t* root_data = kmalloc(*root_size);
    if (!root_data) return NULL;
    for (size_t i = 0; i < fat->root_dir_sectors; i++) {
        uint8_t* sector_buffer = root_data + (i * fat->sector_size);
        uint32_t lba_addr = fat->lba_start + fat->first_root_dir_sector + i;
        int res = fat_open_sector(fat, sector_buffer, fat->sector_size, lba_addr);
        if (res != 0) {
            kfree(root_data);
            return NULL;
        }
    }
    return root_data;
}

/**
 * @brief Iterates over directory entries in a FAT16 directory buffer.
 *
 * This function scans a buffer containing FAT16 directory entries and invokes
 * a callback function for each valid 32-byte entry. The iteration stops if an
 * empty entry (0x00) is encountered or if the callback function returns a
 * nonzero value.
 *
 * @param dir_data Pointer to the buffer containing directory entries.
 * @param dir_size Size of the directory buffer in bytes.
 * @param callback Function pointer to process each directory entry. It takes
 *                 two arguments: a pointer to the 32-byte entry and a
 *                 user-defined context.
 * @param context  A user-defined context pointer passed to the callback
 *                 function.
 * @returns 0 on successful iteration, the callback's return value if nonzero,
 *          or -1 if `dir_data` is NULL.
 *
 * @note The callback function should return 0 to continue iteration, a
 *       positive value to stop early, or -1 to indicate an error.
 */
static int fat_process_dir_entries(uint8_t* dir_data, size_t dir_size,
                                   int (*callback)(const void* entry, void* context), void* context)
{
    if (!dir_data || !callback) return -1;
    for (size_t entry_offset = 0; entry_offset < dir_size; entry_offset += 32) {
        if (dir_data[entry_offset] == 0x0) break;
        // NOTE: Might want to callback in case the caller is looking for
        // deleted files.
        if (dir_data[entry_offset] == 0xE5) continue;
        int res = callback(dir_data + entry_offset, context);
        if (res) return res;
    }
    return 0;
}

// TODO: Rework mount_t so it stores the FAT BPB properly and rework this
// function to use the values stored in there.

/**
 * @brief Scans a FAT directory and processes its entries using a callback.
 *
 * This function iterates over a directory's entries, either in the root
 * directory (fixed location) or a subdirectory (linked via FAT cluster chain).
 * Each entry is passed to the provided callback function for processing.
 *
 * @param mount Pointer to the mounted FAT filesystem.
 * @param start_cluster First cluster of the directory to scan (0 for root).
 * @param callback Function pointer to process each directory entry.
 * @param context User-defined context pointer passed to the callback.
 * @returns 0 if all entries were processed, a positive value if the callback
 *          requested early termination, or -1 on error.
 *
 * @note The callback should return 0 to continue, >0 to stop early, or -1
 *       to signal an error.
 */
static int fat_scan_dir(mount_t* mount, uint32_t start_cluster, int (*callback)(const void* entry, void* context),
                        void* context)
{
    if (callback == NULL) {
        puts("callback can't be NULL");
        return -1;
    }
    // If we need to scan the root directory
    if (start_cluster == 0) {
        // Read data from root directory into buffer
        size_t root_size;
        uint8_t* root_data = fat_read_root_dir(fat, &root_size);
        if (root_data == NULL) return -1;
        int res = fat_process_dir_entries(root_data, root_size, callback, context);
        kfree(root_data);
        return res;
    }
    uint8_t* cluster_buffer = kmalloc(fat->cluster_size);
    uint32_t next_cluster = start_cluster;
    int res = 0;
    while (next_cluster < FAT_END_OF_CHAIN) {
        res = fat_open_cluster(fat, cluster_buffer, fat->cluster_size, next_cluster);
        if (res) {
            kfree(cluster_buffer);
            return res;
        }
        res = fat_process_dir_entries(cluster_buffer, fat->cluster_size, callback, context);
        if (res) {
            kfree(cluster_buffer);
            return res;
        }
        next_cluster = fat_get_next_cluster(fat, next_cluster);
    }

    kfree(cluster_buffer);
    return 0;
}

/// Fills filetables, num_tables becomes number of found files
/// Expects entire fat_table (cluster)
static void parse_directory(struct fat_fs* fs, fat_filetable_t* tables, size_t max_tables, size_t* num_tables,
                            uint8_t* fat_table, size_t table_size)
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
 * PREVIOUS CLUSTER INSTEAD?)
 *
 * @returns cluster
 */
static uint32_t fat_get_next_cluster(const struct fat_fs* fs, uint32_t prev_cluster)
{
    // TODO: document basic blurb on how this all works

    // Setting up offsets
    uint32_t active_cluster = prev_cluster;
    unsigned char FAT_table[fs->sector_size];
    unsigned int fat_offset = active_cluster * 2;
    // Have to add lba_start, TODO: make lba start included in first_fat sector?
    unsigned int fat_sector = fs->first_fat_sector + (fat_offset / fs->sector_size) + fs->lba_start;
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

/**
 * @brief Reads entire cluster into buffer
 *
 * @param ...
 * @param cluster Cluster to open. If cluster is root directory specify 0.
 *
 */
static int fat_open_cluster(const struct fat_fs* fs, uint8_t* buffer, size_t buffer_size, uint32_t cluster)
{
    uint32_t offset, lba_addr;

    // Cluster == 0 if root section
    if (cluster == 0) {
        offset = fs->first_root_dir_sector;
        lba_addr = fs->lba_start + offset;

    } else {
        offset = fs->first_data_sector;
        lba_addr = fs->lba_start + offset + ((cluster - 2) * fat_boot->sectors_per_cluster);
    }
    // Holds entire cluster
    uint8_t* cluster_data = kmalloc(fs->cluster_size);
    // printf("cluster: %d, lba_addr: %d\n", cluster, lba_addr);
    for (size_t i = 0; i < fat_boot->sectors_per_cluster; i++) {
        uint8_t* tmp = cluster_data + (i * fs->sector_size);
        fat_open_sector(fs, tmp, fat->sector_size, lba_addr + i);
    }
    if (buffer_size <= fs->cluster_size) {
        memcpy(buffer, cluster_data, buffer_size);
    } else {
        memcpy(buffer, cluster_data, fs->cluster_size);
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
static int fat_open_sector(const struct fat_fs* fs, uint8_t* buffer, uint16_t buffer_size, uint32_t sector)
{
    uint16_t read_buff[256] = { 0 };
    // printf("Reading from sector: %d\n", sector);
    if (!fs->device->rw_handler(fs->device, OP_READ, read_buff, sector, fs->device->sec_size, 1)) {
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

/// Random function to call to test certain helper functions
void fat_test()
{
    printf("Testing FAT16 Filename Normalization:\n\n");

    test_fat_normalize_filename("HELLO   TXT", "HELLO.TXT");
    sleep(1000);
    test_fat_normalize_filename("HEL LO  TXT", "HEL LO.TXT");
    sleep(1000);
    test_fat_normalize_filename("        ", "");
    sleep(1000);
    test_fat_normalize_filename("WORLD   DOC", "WORLD.DOC");
    sleep(1000);
    test_fat_normalize_filename("PROGRAM EXE", "PROGRAM.EXE");
    test_fat_normalize_filename("DIR        ", "DIR");
}
