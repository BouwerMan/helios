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

// TODO: Figure out how to properly switch between different FAT versions

static struct fat_BS* fat_boot;
// TODO: setup proper storage of filesystems
static struct fat_fs* fat;

static int fat_open_sector(const struct fat_fs* fs, uint8_t* buffer,
                           uint16_t buffer_size, uint32_t sector);
static int fat_open_cluster(const struct fat_fs* fs, uint8_t* buffer,
                            size_t buffer_size, uint32_t cluster);
static uint32_t fat_get_next_cluster(const struct fat_fs* fs,
                                     uint32_t prev_cluster);

static uint8_t* fat_read_root_dir(struct fat_fs* fat, size_t* root_size);
static int fat_scan_dir(struct fat_fs* fat, uint32_t start_cluster,
                        int (*callback)(const void* entry, void* context),
                        void* context);
static int fat_process_dir_entries(uint8_t* dir_data, size_t dir_size,
                                   int (*callback)(const void* entry,
                                                   void* context),
                                   void* context);
static int fat_lookup_inode_callback(const void* entry, void* context);

struct vfs_fs_type fat16_fs_type
    = { .name = "fat16", .fs_type = FAT16, .mount = fat16_mount, .next = NULL };

/**
 * @brief Initializes the FAT filesystem driver.
 *
 * This function registers the FAT16 filesystem type with the Virtual File
 * System (VFS), enabling the system to recognize and mount FAT16 partitions.
 * It should be called during the system initialization phase.
 */
void fat_init() { register_filesystem(&fat16_fs_type); }

/**
 * @brief Mounts a FAT16 filesystem on the specified ATA device.
 *
 * This function initializes and mounts a FAT16 filesystem located at the
 * given Logical Block Address (LBA) on the specified ATA device. It returns
 * a pointer to the corresponding virtual filesystem superblock.
 *
 * @param device    Pointer to the ATA device structure.
 * @param lba_start The starting LBA of the FAT16 partition.
 * @param flags     Mount flags to configure the filesystem behavior.
 * @return          Pointer to the mounted vfs_superblock structure, or NULL
 *                  on failure.
 */
struct vfs_superblock* fat16_mount(sATADevice* device, uint32_t lba_start,
                                   int flags)
{
    (void)flags;

    struct vfs_superblock* sb = kmalloc(sizeof(struct vfs_superblock));
    if (!sb) return NULL;

    // Create and fill fat boot sector
    struct fat_BS* bs = kmalloc(sizeof(struct fat_BS));
    if (!bs) goto clean_sb;
    int res = fat16_read_boot_sector(device, lba_start, bs);
    if (res < 0) goto clean_bs;

    // Fill filesystem specific information
    struct fat_fs* fs = kmalloc(sizeof(struct fat_fs));
    if (!fs) goto clean_bs;
    fat_fill_meta(bs, fs);
    fs->lba_start = lba_start;
    fs->device = device;
    fs->bs = bs;

    sb->fs_type = &fat16_fs_type;
    sb->fs_data = fs;

    // Fill root directory entry
    struct vfs_dentry* root_dentry = kmalloc(sizeof(struct vfs_dentry));
    if (!root_dentry) goto clean_fs;
    static const int root_len = 2; // strlen("/")
    root_dentry->name = kmalloc(sizeof(root_len));
    strncpy(root_dentry->name, "/", root_len);
    if (!root_dentry->name) goto clean_dentry;
    root_dentry->inode = fat16_get_root_inode(sb);
    if (!root_dentry->inode) goto clean_name;
    root_dentry->fs_data = fs;
    root_dentry->parent = NULL;
    root_dentry->ref_count = 1;
    root_dentry->flags = DENTRY_DIR | DENTRY_ROOT;
    sb->root_dentry = root_dentry;

    return sb;

clean_name:
    kfree(root_dentry->name);
clean_dentry:
    kfree(root_dentry);
clean_fs:
    kfree(fs);
clean_bs:
    kfree(bs);
clean_sb:
    kfree(sb);
    return NULL;
}

/**
 * @brief Reads the boot sector of a FAT16 filesystem.
 *
 * This function reads the boot sector of a FAT16 filesystem from the specified
 * ATA device and Logical Block Address (LBA). The boot sector information is
 * stored in the provided struct fat_BS structure.
 *
 * @param device    Pointer to the ATA device structure.
 * @param lba_start The starting LBA of the FAT16 partition.
 * @param bs        Pointer to a struct fat_BS structure to store the boot
 * sector data.
 * @return          1 on success, -1 on failure.
 */
int fat16_read_boot_sector(sATADevice* device, uint32_t lba_start,
                           struct fat_BS* bs)
{
    uint16_t buffer[256] = { 0 };
    printf("Attempting to read device %d\n", device->id);
    bool res = device->rw_handler(device, OP_READ, buffer, lba_start,
                                  device->sec_size, 1);
    if (!res) {
        printf("Failed to read device %d\n", device->id);
        return -1;
    }
    memcpy(bs, buffer, sizeof(struct fat_BS));
    return 1;
}

/**
 * @brief Returns the root inode for a FAT16 filesystem.
 *
 * Allocates and initializes a VFS inode representing the root directory
 * of the FAT16 filesystem described by `sb`. Also sets up FAT-specific
 * inode information.
 *
 * @param sb Pointer to the superblock containing FAT16 filesystem data.
 * @return Pointer to the root inode on success, or NULL on failure.
 *
 * @note Caller must manage the lifetime of the returned inode.
 */
struct vfs_inode* fat16_get_root_inode(struct vfs_superblock* sb)
{
    if (!sb) return NULL;
    struct fat_fs* fs = (struct fat_fs*)sb->fs_data;
    struct fat_BS* bs = fs->bs;
    struct vfs_inode* r_node = kmalloc(sizeof(struct vfs_inode));
    if (!r_node) return NULL;

    r_node->id = 0;
    r_node->filetype = FILETYPE_DIR;
    r_node->f_size = fs->root_dir_sectors * bs->bytes_per_sector;
    r_node->ref_count = 1;
    r_node->permissions
        = VFS_PERM_ALL; // TODO: use stricter perms once supported.
    r_node->flags = 0;
    struct fat_inode_info* i_info = kmalloc(sizeof(struct fat_inode_info));
    if (!i_info) goto clean_vnode;
    i_info->fat = (struct fat_fs*)(sb->fs_data);
    i_info->fat_variant = FAT16;
    i_info->init_cluster = 0;
    i_info->current_cluster = 0;
    i_info->chain_len = 0;
    i_info->dir_cluster = 0;
    i_info->dir_offset = 0;
    i_info->fat_attrib = FAT_SYSTEM | FAT_DIRECTORY;

    r_node->fs_data = i_info;

    return r_node;
clean_vnode:
    kfree(r_node);
    return NULL;
}

static void fat_print_meta(struct fat_fs* fs)
{
    printf("FAT Type: %d\n", fs->fat_type);
    printf("Sector size: %d bytes\n", fs->sector_size);
    printf("Total clusters: %d\n", fs->total_sectors);

    printf("Cluster size: %d bytes\n", fs->cluster_size);
    printf("Total clusters: %d\n", fs->total_clusters);

    printf("Fat size: %d bytes\n", fs->fat_size);
    printf("First fat sector offset: %d\n", fs->first_fat_sector);
    printf("First root_dir sector offset: %d\n", fs->first_root_dir_sector);
    printf("First data sector offset: %d\n", fs->first_data_sector);
}

/**
 * @brief Calculates important metadata to fill 'fs'.
 *
 * @note The function does not fill `fs->lba_start` and `fs->device`. The caller
 * is repsonsible for those fields.
 *
 * @param   bs  FAT BIOS Parameter Block to read from.
 * @param   fs  FAT metadata info to fill.
 */
void fat_fill_meta(struct fat_BS* bs, struct fat_fs* fs)
{
    fs->total_sectors = (bs->total_sectors_16 == 0) ? bs->total_sectors_32
                                                    : bs->total_sectors_16;
    fs->fat_size = bs->table_size_16;
    fs->sector_size = bs->bytes_per_sector;
    fs->cluster_size = fs->sector_size
        * bs->sectors_per_cluster; // number of bytes in a cluster
    fs->root_dir_sectors
        = ((bs->root_entry_count * 32) + (bs->bytes_per_sector - 1))
        / bs->bytes_per_sector;
    fs->data_sectors = fs->total_sectors
        - (bs->reserved_sector_count + (bs->table_count * fs->fat_size)
           + fs->root_dir_sectors);
    fs->total_clusters = fs->data_sectors / bs->sectors_per_cluster;

    // TODO: Find a better way to figure out fat type. or anyway at this point
    fs->fat_type = FAT16;

    fs->first_fat_sector = bs->reserved_sector_count;
    fs->first_data_sector = bs->reserved_sector_count
        + (bs->table_count * fs->fat_size) + fs->root_dir_sectors;
    fs->first_root_dir_sector = fs->first_data_sector - fs->root_dir_sectors;
    fat_print_meta(fs);
}

/**
 * @brief Looks up a child directory entry by name within a given directory
 * inode.
 *
 * This function searches for a child directory entry with the name specified
 * in the `child->name` field within the directory represented by `dir_inode`.
 * If found, it initializes the `child->inode` field with the corresponding
 * inode structure. The function allocates memory for the inode and its
 * associated FAT-specific data structure.
 *
 * @param dir_inode Pointer to the directory inode where the search is
 * performed. Must represent a directory (FILETYPE_DIR).
 * @param child     Pointer to the dentry structure containing the name of the
 *                  child to look up. On success, its `inode` field is
 * populated.
 *
 * @return Pointer to the `child` dentry structure. If the lookup fails, the
 *         `child->inode` field is set to NULL.
 *
 * @note The caller is responsible for ensuring that `dir_inode` is a valid
 *       directory inode. Memory allocated for the inode and its FAT-specific
 *       data structure is freed in case of failure.
 */
struct vfs_dentry* fat_lookup(struct vfs_inode* dir_inode,
                              struct vfs_dentry* child)
{
    if (dir_inode->filetype != FILETYPE_DIR) goto clean;
    struct fat_inode_info* fat_info = dir_inode->fs_data;
    struct vfs_inode* inode = kmalloc(sizeof(struct vfs_inode));
    if (inode == NULL) goto clean;
    child->inode = inode;
    struct fat_inode_info* fat_inode = kmalloc(sizeof(struct fat_inode_info));
    if (fat_inode == NULL) goto clean_inode;
    inode->fs_data = fat_inode;

    int res = fat_scan_dir(fat_info->fat, fat_info->init_cluster,
                           fat_lookup_inode_callback, (void*)child);
    if (res <= 0) goto clean_fat_inode;
    inode->id = vfs_get_next_id();
    inode->ref_count = 1;
    inode->permissions = VFS_PERM_ALL;
    inode->flags = 0;

    fat_inode->fat = fat_info->fat;
    fat_inode->fat_variant = FAT16;
    fat_inode->current_cluster = 0;
    fat_inode->dir_cluster = fat_info->init_cluster;

    child->inode = inode;
    return child;

clean_fat_inode:
    kfree(fat_inode);
clean_inode:
    kfree(inode);
clean:
    child->inode = NULL;
    return child;
}

// TODO: Ignores file extensions
// TODO: Support directories
// TODO: Account for filesize
int fat_open_file(const inode_t* inode, char* buffer, size_t buffer_size)
{
    uint32_t cluster_size = fat->sector_size
        * fat_boot->sectors_per_cluster; // number of bytes in a cluster

    uint32_t next_cluster = inode->init_cluster;
    size_t i = 0;
    while (next_cluster < FAT_END_OF_CHAIN) {
        next_cluster = fat_get_next_cluster(fat, next_cluster);
        fat_open_cluster(fat, (uint8_t*)(buffer + (i * cluster_size)),
                         buffer_size - (i * cluster_size), next_cluster);
        i++;
    }

    return 0;
}

void fat_close_file(void* file_start) { kfree(file_start); }

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
    for (size_t i = 0; i < name_len && i < target_len; i++) {
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
 * @param entry     Pointer to a 32-byte FAT directory entry.
 * @param context   Pointer to a `vfs_dentry` structure where file
 *                  metadata will be stored.
 * @returns 1 if the file is found (stop scanning), 0 to continue scanning,
 *          or -1 on error.
 *
 * @note The filename comparison is case-insensitive, and spaces in FAT
 *       filenames are ignored.
 */
int fat_lookup_inode_callback(const void* entry, void* context)
{
    struct fat_filetable file_table;
    struct vfs_dentry* dentry = context;
    struct fat_fs* fat = dentry->fs_data;
    struct vfs_inode* inode = dentry->inode;
    struct fat_inode_info* fat_inode = inode->fs_data;

    memcpy(&file_table, entry, 32);
    char norm_name[13] = { 0 };
    fat_normalize_filename(entry, norm_name);
    if (fat_compare_filenames(norm_name, dentry->name)) {
        // Populate inode with file metadata
        inode->filetype = (file_table.attrib & FAT_DIRECTORY) ? FILETYPE_DIR
                                                              : FILETYPE_FILE;
        inode->f_size = file_table.size; // File size (bytes)
        fat_inode->init_cluster = file_table.cluster;
        fat_inode->chain_len
            = file_table.size / fat->cluster_size + 1; // Kinda rough estimate
        fat_inode->fat_attrib = file_table.attrib;

        return 1; // Stop scanning, file found
    }
    return 0;
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
        int res
            = fat_open_sector(fat, sector_buffer, fat->sector_size, lba_addr);
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
                                   int (*callback)(const void* entry,
                                                   void* context),
                                   void* context)
{
    if (!dir_data || !callback) return -1;
    for (size_t entry_offset = 0; entry_offset < dir_size; entry_offset += 32) {
        if (dir_data[entry_offset] == 0x0) break;
        // NOTE: Might want to callback in case the caller is looking
        // for deleted files.
        if (dir_data[entry_offset] == 0xE5) continue;
        int res = callback(dir_data + entry_offset, context);
        if (res) return res;
    }
    return 0;
}

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
static int fat_scan_dir(struct fat_fs* fat, uint32_t start_cluster,
                        int (*callback)(const void* entry, void* context),
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
        int res
            = fat_process_dir_entries(root_data, root_size, callback, context);
        kfree(root_data);
        return res;
    }
    uint8_t* cluster_buffer = kmalloc(fat->cluster_size);
    uint32_t next_cluster = start_cluster;
    int res = 0;
    while (next_cluster < FAT_END_OF_CHAIN) {
        res = fat_open_cluster(fat, cluster_buffer, fat->cluster_size,
                               next_cluster);
        if (res) {
            kfree(cluster_buffer);
            return res;
        }
        res = fat_process_dir_entries(cluster_buffer, fat->cluster_size,
                                      callback, context);
        if (res) {
            kfree(cluster_buffer);
            return res;
        }
        next_cluster = fat_get_next_cluster(fat, next_cluster);
    }

    kfree(cluster_buffer);
    return 0;
}

/**
 * @brief gets next cluster in chain
 * @param[in] first_cluster First cluster of the file (DOES THIS NEED TO BE
 * PREVIOUS CLUSTER INSTEAD?)
 *
 * @returns cluster
 */
static uint32_t fat_get_next_cluster(const struct fat_fs* fs,
                                     uint32_t prev_cluster)
{
    // TODO: document basic blurb on how this all works

    // Setting up offsets
    uint32_t active_cluster = prev_cluster;
    unsigned char FAT_table[fs->sector_size];
    unsigned int fat_offset = active_cluster * 2;
    // Have to add lba_start, TODO: make lba start included in first_fat
    // sector?
    unsigned int fat_sector
        = fs->first_fat_sector + (fat_offset / fs->sector_size) + fs->lba_start;
    unsigned int ent_offset = fat_offset % fs->sector_size;

    // at this point you need to read from sector "fat_sector" on the disk
    // into "FAT_table".
    //  TODO: Handle errors
    fat_open_sector(fs, FAT_table, fs->sector_size,
                    fat_sector); // First sector

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
 * @param cluster Cluster to open. Must be from the data region
 *
 */
static int fat_open_cluster(const struct fat_fs* fs, uint8_t* buffer,
                            size_t buffer_size, uint32_t cluster)
{
    uint32_t lba_addr;
    puts("Opened cluster lets goooo");

    lba_addr = fs->lba_start + fs->first_data_sector
        + ((cluster - 2) * fs->bs->sectors_per_cluster);

    // Holds entire cluster
    uint8_t* cluster_data = kmalloc(fs->cluster_size);
    // printf("cluster: %d, lba_addr: %d\n", cluster, lba_addr);
    fs->device->rw_handler(fs->device, OP_READ, cluster_data, lba_addr,
                           fs->device->sec_size, fs->bs->sectors_per_cluster);
    // for (size_t i = 0; i < fat_boot->sectors_per_cluster; i++) {
    //     uint8_t* tmp = cluster_data + (i * fs->sector_size);
    //     fat_open_sector(fs, tmp, fat->sector_size, lba_addr + i);
    // }
    if (buffer_size <= fs->cluster_size) {
        memcpy(buffer, cluster_data, buffer_size);
    } else {
        memcpy(buffer, cluster_data, fs->cluster_size);
    }
    kfree(cluster_data);
    puts("Finished opened cluster lets goooo");
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
    if (!fs->device->rw_handler(fs->device, OP_READ, read_buff, sector,
                                fs->device->sec_size, 1)) {
        printf("Could not read from disk\n");
        return -1;
    }
    // Copy into caller's buffer, manually making sure we don't read past
    // end of read_buff
    if (buffer_size <= 512) {
        memcpy(buffer, read_buff, buffer_size);
    } else {
        memcpy(buffer, read_buff, 512);
    }
    return 0;
}
