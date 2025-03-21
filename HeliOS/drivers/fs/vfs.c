#include <drivers/fs/fat.h>
#include <drivers/fs/vfs.h>
#include <kernel/liballoc.h>
#include <stdio.h>

struct vfs_fs_type* fs_list = NULL;

mount_t* mounts;
static uint8_t max_mounts;
static uint8_t mount_idx;

filesystem_t* filesystems;
static uint8_t max_fs;
static uint8_t fs_idx = 0;

static void register_mount(mount_t mount);

static inode_t** inode_cache;
// Size of inode cache
static size_t ic_size;
// Last inserted inode
static size_t ic_idx = 0;

void vfs_init(uint8_t maximum_filesystems, uint8_t maximum_mounts,
    size_t inode_cache_size)
{
    max_fs = maximum_filesystems;
    filesystems = (filesystem_t*)kmalloc(sizeof(filesystem_t) * max_fs);
    max_mounts = maximum_mounts;
    mounts = (mount_t*)kmalloc(sizeof(mount_t) * max_mounts);
    ic_size = inode_cache_size;
    inode_cache = (inode_t**)kmalloc((sizeof(uintptr_t)) * ic_size);
}

/// Finds file system in list of supported filesystems
static filesystem_t* find_filesystem(uint8_t fs_type)
{
    for (uint8_t i = 0; i < fs_idx; i++) {
        if (filesystems[i].fs_type == fs_type) return filesystems + i;
    }
    return NULL;
}

bool register_fs(uint8_t fs)
{
    if (find_filesystem(fs)) return false;
    if (fs == FAT16) {
        filesystem_t filesys;
        filesys.id = fs_idx;
        filesys.fs_type = fs;
        filesys.fs_init = init_fat;
        filesys.read_handler = fat_open_file;
        filesys.find_inode = fat_find_inode;
        filesystems[fs_idx++] = filesys;
        return true;
    } else {
        printf("FS not supported\n");
        return false;
    }
}

/// Finds inode based on directory information
/// NOTE: Always turns inode into a malloced structure
static inode_t* find_inode(const dir_t* dir)
{
    for (size_t i = 0; i < ic_idx; i++) {
        inode_t* curr = inode_cache[i];
        if (curr == NULL) continue;
        if (curr->mount->id != dir->mount_id) continue;
        if (curr->dir->path != dir->path) continue;
        if (curr->dir->filename != dir->filename) continue;
        // if (curr.dir->file_extension != dir->file_extension) continue;
        return inode_cache[i];
    }
    return kcalloc(1, sizeof(inode_t));
}

static void cache_inode(inode_t* inode)
{
    printf("Caching at %d\n", ic_idx);
    inode->id = ic_idx;
    inode_cache[ic_idx++] = inode;
}

static void uncache_inode(inode_t* inode)
{
    inode_cache[inode->id] = NULL;
    kfree(inode);
}

FILE* vfs_open(dir_t* directory)
{
    if (directory->mount_id > mount_idx) return NULL;
    void* file_ptr = NULL;
    inode_t* file_inode = find_inode(directory);
    puts("Searched for inode");
    // First we check if we have already cached this inode
    if (file_inode->f_size == 0) {
        puts("Had to ask filesystem to find inode");
        // If we can't find the inode we ask the filesystem driver to search the
        // drive We fill the inode slightly to aide driver searching
        file_inode->dir = directory;
        file_inode->mount = mounts + directory->mount_id;
        // If the driver can't find it then we return NULL
        if (mounts[directory->mount_id].filesystem->find_inode(file_inode))
            return NULL;
        // Cache the inode since we found it
        cache_inode(file_inode);
    }
    puts("Trying to read file");
    FILE* file = kmalloc(sizeof(FILE));
    char* file_buff = kmalloc(file_inode->f_size);
    int res = mounts[directory->mount_id].filesystem->read_handler(
        file_inode, file_buff, file_inode->f_size);
    // If successful we can return
    if (res == 0) {
        puts("Successfully read file");
        file->file_ptr = file_buff;
        file->read_ptr = file_buff;
        file->file_size = file_inode->f_size;
        return file;
    }
    puts("Did not read file");
    // if not successful we free the memory
    kfree(file_inode);
    kfree(file);
    kfree(file_buff);
    return NULL;
}

void vfs_close(FILE* file)
{
    // TODO: should probably flush buffers or smthn. Maybe update meta data
    kfree(file->file_ptr);
    kfree(file);
}

static void register_mount(mount_t mount)
{
    printf("Registering mount with id %d\n", mount.id);
    mounts[fs_idx] = mount;
    mounts[fs_idx].present = true;
    fs_idx++;
}
void unregister_mount(mount_t mount) { mounts[mount.id].present = false; }

int mount(
    uint8_t id, sATADevice* device, sPartition* partition, uint8_t fs_type)
{
    if (id > max_mounts || mounts[id].present) return -1;
    // building mount struct
    mount_t mount;
    mount.device = device;
    mount.partition = partition;
    mount.id = id;
    mount.present = partition->present;
    mount.filesystem = find_filesystem(fs_type);
    // If it is present we add it to the array and init filesystem
    if (mount.present) {
        printf("Adding mount to %d\n", id);
        mounts[id] = mount;
        puts("Initializing filesystem");
        mount.filesystem->fs_init(device, partition->start);
        return 0;
    }
    return 1;
}

void register_filesystem(struct vfs_fs_type* fs)
{
    fs->next = fs_list; // Add fs to beginning of list
    fs_list = fs;
}
