/**
 * @file drivers/fs/vfs.c
 *
 * Copyright (C) 2025  Dylan Parks
 *
 * This file is part of HeliOS
 *
 * HeliOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <string.h>

#include <drivers/fs/fat.h>
#include <drivers/fs/vfs.h>
#include <kernel/liballoc.h>
#include <kernel/panic.h>
#include <mm/slab.h>

#include <util/ht.h>
#include <util/log.h>

// TODO: Find a better way to handle some of these icky globals, also def need some locks

struct vfs_fs_type* fs_list = NULL;
struct vfs_mount* mount_list = NULL;
struct vfs_superblock* rootfs = NULL;
struct vfs_superblock** sb_list;
static uint8_t sb_idx = 0;

struct slab_cache dentry_cache = { 0 };

struct ht* dentry_ht;

static void register_mount(struct vfs_mount* mnt);

static void add_superblock(struct vfs_superblock* sb)
{
	if (sb_idx >= 8) return;
	sb_list[sb_idx++] = sb;
}

/**
 * vfs_init - Initializes the virtual filesystem.
 * @dhash_size: The size of the dentry hash table (must be a power of 2).
 *
 * This function sets up the initial structures required for the virtual
 * filesystem, including the superblock list, dentry hash table, and
 * filesystem initialization. It allocates memory for the superblock list
 * and creates the dentry hash table with the specified size. Additionally,
 * it initializes the FAT filesystem as a placeholder.
 *
 * Note:
 * - The `dhash_size` parameter must be a power of 2 for proper hash table
 *   functionality.
 * - Dentry destructors are not yet implemented.
 * - Filesystem initialization is currently limited to FAT.
 */
void vfs_init(size_t dhash_size)
{
	sb_list = kmalloc(sizeof(uintptr_t) * 8);

	int res = slab_cache_init(&dentry_cache, "VFS Dentry", sizeof(struct vfs_dentry), 8, NULL, NULL);
	if (res < 0) {
		log_error("Could not init dentry cache, slab_cache_init() returned %d", res);
		panic("Dentry cache init failure");
	}

	// TODO: Implement dentry destructors
	dentry_ht = ht_create(dhash_size);
	dentry_ht->ops->hash = dentry_hash;
	dentry_ht->ops->compare = dentry_compare;

	// TODO: Better way to init all the filesystems
	fat_init();
}

/**
 * find_filesystem - Finds a filesystem in the list of supported filesystems.
 * @fs_type: The type of the filesystem to find (e.g., FAT32, EXT2).
 *
 * This function iterates through the global list of registered filesystems
 * and searches for a filesystem that matches the specified type.
 *
 * Return:
 *  - Pointer to the vfs_fs_type structure representing the filesystem if found.
 *  - NULL if no matching filesystem is found.
 */
static struct vfs_fs_type* find_filesystem(uint8_t fs_type)
{
	struct vfs_fs_type* p = fs_list;
	while (p) {
		if (p->fs_type == fs_type) return p;
		p = p->next;
	}
	return NULL;
}

/**
 * dget - Increments the reference count of a dentry.
 * @dentry: Pointer to the vfs_dentry structure whose reference count is to be
 * incremented.
 *
 * This function increases the reference count of the specified dentry,
 * indicating that it is being used by another entity. It ensures that the
 * dentry is not prematurely freed while still in use.
 *
 * Return:
 *  - Pointer to the same dentry structure.
 */
struct vfs_dentry* dget(struct vfs_dentry* dentry)
{
	dentry->ref_count++;
	return dentry;
}

/**
 * dentry_add - Adds a dentry to the hash table.
 * @dentry: Pointer to the vfs_dentry structure to be added.
 */
void dentry_add(struct vfs_dentry* dentry)
{
	ht_set(dentry_ht, dentry, dentry);
}

/**
 * dentry_lookup - Searches for a child dentry within a parent dentry.
 * @parent: The parent dentry to search within.
 * @name: The name of the file or directory to search for.
 *
 * This function attempts to locate a child dentry by first checking a hash
 * table for an existing entry. If not found, it invokes the parent inode's
 * lookup operation to resolve the dentry. If the lookup fails, the returned
 * dentry will have a NULL inode, indicating a negative dentry.
 *
 * Return: Pointer to the found dentry, or NULL on failure.
 */
struct vfs_dentry* dentry_lookup(struct vfs_dentry* parent, const char* name)
{
	// FIXME: I'm doing some funky stuff between inodes and dentrys that
	// probably is bad
	struct vfs_dentry* found;
	// Have to allocate some stuff beforehand for the filesystem and hash table checks
	struct vfs_dentry* child = slab_alloc(&dentry_cache);
	if (!child) return NULL;
	child->name = strdup(name);
	if (!child->name) {
		slab_free(&dentry_cache, child);
		return NULL;
	}
	child->parent = parent;
	child->fs_data = parent->fs_data;
	// Check hash table first if found exit
	if ((found = ht_get(dentry_ht, child)) != NULL) {
		// Since we found it, we free all the child init stuff then return found
		kfree(child->name);
		slab_free(&dentry_cache, child);
		return dget(found);
	}

	if (!parent->inode || !parent->inode->ops || !parent->inode->ops->lookup) {
		log_error("Invalid inode operations");
		kfree(child->name);
		slab_free(&dentry_cache, child);
		return NULL; // Handle invalid inode or missing lookup operation
	}

	// Can just return the end result, if not found then child is a negative
	// dentry (inode == NULL). Otherwise it was properly found.
	return parent->inode->ops->lookup(parent->inode, child);
}

#define FNV_PRIME  0x01000193 ///< The FNV prime constant.
#define FNV_OFFSET 0x811c9dc5 ///< The FNV offset basis constant.

/**
 * Computes a hash for a directory entry (dentry).
 *
 * This function generates a hash value based on the parent inode ID and the
 * name of the directory entry. It uses the FNV-1a hash algorithm for hashing.
 *
 * @param key A pointer to the `vfs_dentry` structure representing the directory
 *            entry.
 * @return A 32-bit hash value for the directory entry.
 */
uint32_t dentry_hash(const void* key)
{
	struct vfs_dentry* dkey = (struct vfs_dentry*)key;
	uint32_t hash = FNV_OFFSET;
	// Initially hash the parent inode id
	uint8_t* id_bytes = (uint8_t*)&dkey->parent->inode->id;
	for (int i = 0; i < 4; i++) {
		hash ^= id_bytes[i];
		hash *= FNV_PRIME;
	}

	// Use that as a starting point for the name hash
	for (const char* p = dkey->name; *p; p++) {
		hash ^= (uint32_t)(unsigned char)(*p);
		hash *= FNV_PRIME;
	}

	return hash;
}

/**
 * Compares two directory entries (dentries) for equality.
 *
 * This function checks if two `vfs_dentry` structures are equal by comparing
 * their names and the IDs of their parent inodes.
 *
 * @param key1 A pointer to the first `vfs_dentry` structure.
 * @param key2 A pointer to the second `vfs_dentry` structure.
 * @return `true` if the dentries are equal, `false` otherwise.
 */
bool dentry_compare(const void* key1, const void* key2)
{
	struct vfs_dentry* dkey1 = (struct vfs_dentry*)key1;
	struct vfs_dentry* dkey2 = (struct vfs_dentry*)key2;
	return (strcmp(dkey1->name, dkey2->name) == 0) && (dkey1->parent->inode->id == dkey2->parent->inode->id);
}

/**
 * vfs_open - Opens a file in the virtual filesystem.
 * @path: The path to the file to be opened.
 * @file: Pointer to the vfs_file structure where file details will be stored.
 *
 * This function resolves the given file path, validates the inode, allocates
 * memory for the file buffer, and invokes the filesystem-specific open
 * operation. It ensures proper error handling and resource management.
 *
 * Return:
 *  - 0 on success.
 *  - -1 if path resolution fails or the inode is invalid.
 *  - -2 if memory allocation fails.
 *  - Error code from the filesystem-specific open operation on failure.
 */
int vfs_open(const char* path, struct vfs_file* file)
{
	// Just going to have this do the path walking
	// linux kernel takes inode as argument, might want that in the future.
	// Would probably jsut confuse myself because then you need a wrapper
	// function (open()) that travels the path and then calls this for fs
	// specific open().

	struct vfs_dentry* dentry = vfs_resolve_path(path);
	if (!dentry) return -1;
	if (!dentry->inode) return -1;

	if (dentry->inode->f_size == 0) return -1; // Invalid file size
	file->file_ptr = kmalloc(dentry->inode->f_size);
	if (!file->file_ptr) return -2; // mem alloc failure

	file->file_size = dentry->inode->f_size;
	file->read_ptr = file->file_ptr;

	int ret = dentry->inode->ops->open(dentry->inode, file);
	if (ret < 0) {
		kfree(file->file_ptr);
		return ret;
	}
	return 0;
}

/**
 * vfs_close - Closes a file in the virtual filesystem.
 * @file: Pointer to the vfs_file structure representing the file to be closed.
 */
void vfs_close(struct vfs_file* file)
{
	// TODO: should probably flush buffers or smthn. Maybe update meta data
	kfree(file->file_ptr);
	kfree(file);
}

/**
 * mount - Mounts a filesystem at the specified mount point.
 * @mount_point: The path where the filesystem will be mounted.
 * @device: Pointer to the ATA device structure representing the storage device.
 * @partition: Pointer to the partition structure containing partition details.
 * @fs_type: The type of filesystem to mount (e.g., FAT32, EXT2).
 *
 * This function initializes a mount structure, associates it with the given
 * device and partition, and registers it in the system. If the partition is
 * present, it initializes the filesystem and superblock, and adds them to
 * the system's mount and superblock lists.
 *
 * Return:
 *  - 0 on successful mount and initialization.
 *  - 1 if the partition is not present.
 *  - -1 on failure (e.g., memory allocation failure or missing mount function).
 */
int mount(const char* mount_point, sATADevice* device, sPartition* partition, uint8_t fs_type)
{
	// building mount struct
	struct vfs_mount* mount = kmalloc(sizeof(struct vfs_mount));
	if (!mount) return -1;
	mount->mount_point = strdup(mount_point);
	if (!mount->mount_point) {
		kfree(mount);
		return -1;
	}
	mount->device = device;
	mount->lba_start = (uint32_t)partition->start;
	mount->flags = partition->present ? MOUNT_PRESENT : 0;
	mount->next = NULL;

	// If it is present we add it to the array and init filesystem
	if (mount->flags & MOUNT_PRESENT) {
		log_info("Adding mount");
		register_mount(mount);
		// Set rootfs
		log_info("Initializing filesystem");
		struct vfs_fs_type* fs = find_filesystem(fs_type);
		if (fs->mount == NULL) panic("uh oh mount function dont exist");
		struct vfs_superblock* sb = fs->mount(device, mount->lba_start, 0);
		if (!mount) {
			kfree(mount->mount_point);
			kfree(mount);
			return -1;
		}
		mount->sb = sb;
		add_superblock(sb);
		if (mount->mount_point[0] == '/' && (strlen(mount->mount_point) == 1)) {
			rootfs = mount->sb;
		}
		return 0;
	}
	return 1;
}

/**
 * register_mount - Registers a mount point in the virtual filesystem.
 * @mnt: Pointer to the vfs_mount structure representing the mount point.
 *
 * This function adds the given mount point to the global mount list. If the
 * list is empty, the mount point becomes the head of the list. Otherwise, it
 * is added to the beginning of the list.
 */
static void register_mount(struct vfs_mount* mnt)
{
	if (mount_list == NULL) {
		mount_list = mnt;
	} else {
		mnt->next = mount_list;
		mount_list = mnt;
	}
}

/**
 * register_filesystem - Registers a filesystem type in the virtual filesystem.
 * @fs: Pointer to the vfs_fs_type structure representing the filesystem type.
 *
 * This function adds the given filesystem type to the global filesystem list.
 * If the list is empty, the filesystem type becomes the head of the list.
 * Otherwise, it is added to the beginning of the list.
 */
void register_filesystem(struct vfs_fs_type* fs)
{
	if (fs_list == NULL) {
		fs_list = fs;
	} else {
		fs->next = fs_list; // Add fs to beginning of list
		fs_list = fs;
	}
}

/// Gets superblock for idx
struct vfs_superblock* vfs_get_sb(int idx)
{
	return sb_list[idx];
}

int uuid = 1; // Always points to next available id, 0 = invalid id
/// Returns new unique ID to use
int vfs_get_next_id()
{
	return uuid++;
}
/// Returns most recent allocated id
int vfs_get_id()
{
	return uuid - 1;
}

/**
 * @brief Resolves an absolute path to a VFS dentry, starting from the correct
 * mount point.
 *
 * This function searches the mount table to find the best matching mount point
 * for the given path, then walks the remaining path from the mount's root
 * dentry.
 *
 * @param path   Absolute path to resolve (e.g., "/mnt/usb/file.txt").
 * @return       Pointer to the final vfs_dentry if successful, or NULL on
 * failure.
 */
struct vfs_dentry* vfs_resolve_path(const char* path)
{
	// TODO: Should check/normalize path

	struct vfs_mount* match = NULL;
	size_t match_len = 0;

	// Traverse mount_list
	for (struct vfs_mount* m = mount_list; m; m = m->next) {
		size_t len = strlen(m->mount_point);
		if (strncmp(path, m->mount_point, len) == 0 && (path[len] == '/' || path[len] == '\0')) {
			if (len > match_len) {
				match = m;
				match_len = len;
			}
		}
	}

	if (!match) {
		// FIXME: Doesn't varify rootfs->root_dentry exists
		return vfs_walk_path(rootfs->root_dentry, path + 1);
	}

	// Now create relative path for further fs dentry walking
	const char* rel_path = path + match_len;
	if (*rel_path == '/') rel_path++; // Skip leading '/' (NEEDED?)

	return vfs_walk_path(match->sb->root_dentry, rel_path);
}

/**
 * @brief Resolves a relative path starting from a given root dentry.
 *
 * This function walks the path one component at a time, using the VFS's
 * lookup mechanism (which in turn delegates to the filesystem driver).
 *
 * For example, given root = "/mnt/usb" and path = "dir/file.txt", this
 * function resolves to the dentry for "/mnt/usb/dir/file.txt".
 *
 * @param root     The starting directory dentry (must be a directory).
 * @param path     The relative path to resolve (e.g., "foo/bar.txt").
 * @return         A pointer to the final vfs_dentry on success, or NULL on
 *                 failure.
 */
struct vfs_dentry* vfs_walk_path(struct vfs_dentry* root, const char* path)
{
	const size_t path_len = strlen(path);
	char buffer[path_len + 1];
	strncpy(buffer, path, path_len + 1);
	char* token = strtok(buffer, "/");

	struct vfs_dentry* parent = root;
	while (token != NULL) {
		parent = dentry_lookup(parent, token);
		token = strtok(NULL, "/");
	}
	return parent;
}
