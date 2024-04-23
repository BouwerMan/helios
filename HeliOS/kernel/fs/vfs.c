#include <kernel/fs/vfs.h>
#include <kernel/liballoc.h>
#include <stdio.h>

// TODO: flesh out arguments
typedef void* (*f_read)();

typedef struct mount {
    bool present;
    uint8_t id;
    uint8_t fs;
    f_read read_handler;
} mount_t;

mount_t* mounts;

static uint8_t max_mounts;
static uint8_t mount_idx = 0;

static void register_mount(mount_t mount);

void vfs_init(uint8_t maximum_mounts)
{
    max_mounts = maximum_mounts;
    mounts = (mount_t*)kmalloc(sizeof(mount_t) * max_mounts);
}

bool register_fs(uint8_t fs)
{
    if (fs == FAT16) {
        mount_t mount;
        mount.id = mount_idx;
        mount.fs = fs;
        mount.present = true;
        register_mount(mount);
        return true;
    } else {
        printf("FS not supported\n");
        return false;
    }
}

FILE* vfs_open(dir_t directory)
{
    void* file_ptr = mounts[directory.mount].read_handler();
    FILE* file = kmalloc(sizeof(FILE));
    file->file_ptr = file_ptr;
    file->file_size = 0; // TODO: Implement file size
    return file;
}

void vfs_close(FILE* file)
{
    // TODO: should probably flush buffers or smthn
    kfree(file);
}

static void register_mount(mount_t mount)
{
    printf("Registering mount with id %d\n", mount.id);
    mounts[mount_idx] = mount;
    mounts[mount_idx].present = true;
    mount_idx++;
}
void unregister_mount(mount_t mount) { mounts[mount.id].present = false; }
