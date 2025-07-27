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

#include <stdlib.h>
#include <string.h>

#include <drivers/fs/fat.h>
#include <drivers/fs/ramfs.h>
#include <drivers/fs/vfs.h>
#include <kernel/panic.h>
#include <mm/slab.h>

#include <util/ht.h>
#include <util/log.h>

// TODO: Find a better way to handle some of these icky globals, also def need some locks

/*******************************************************************************
* Global Variable Definitions
*******************************************************************************/

struct vfs_fs_type* fs_list = NULL;
struct vfs_mount* mount_list = NULL;
struct vfs_superblock* rootfs = NULL;
struct vfs_superblock** sb_list;
static uint8_t sb_idx = 0;

struct slab_cache dentry_cache = { 0 };

struct ht* dentry_ht;

static struct vfs_mount* g_vfs_root_mount = nullptr;

struct path_tokenizer {
	const char* path;
	size_t offset;
};

/*******************************************************************************
* Private Function Prototypes
*******************************************************************************/

/**
 * @brief Trims triling characters from string by replacing with '\0'.
 *
 * @param s string to trim
 * @param c character to trim
 */
static void trim_trailing(char* s, char c);
static const char* path_next_token(struct path_tokenizer* tok, size_t* out_len);
static struct vfs_fs_type* find_filesystem(const char* fs_type);
static void register_mount(struct vfs_mount* mnt);

static void add_superblock(struct vfs_superblock* sb)
{
	if (sb_idx >= 8) return;
	sb_list[sb_idx++] = sb;
}

/*******************************************************************************
* Public Function Definitions
*******************************************************************************/

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
	// fat_init();
}

int mount_initial_rootfs()
{
	log_debug("Initializing root filesystem mount.");

	g_vfs_root_mount = kmalloc(sizeof(*g_vfs_root_mount));
	if (!g_vfs_root_mount) {
		log_debug("Failed to allocate memory for root mount.");
		return -1;
	}

	g_vfs_root_mount->mount_point = strdup("/");
	if (!g_vfs_root_mount->mount_point) {
		log_debug("Failed to allocate memory for mount point.");
		goto mount_point_fail;
	}

	log_debug("Mount point set to '/'.");

	struct vfs_superblock* sb = ramfs_mount("/", 0);
	if (!sb) {
		log_debug("Failed to mount ramfs at '/'");
		goto sb_fail;
	}

	log_debug("Ramfs mounted successfully at '/'.");

	g_vfs_root_mount->sb = sb;
	register_mount(g_vfs_root_mount);
	add_superblock(sb);

	log_debug("Root filesystem mount completed successfully.");
	return 0;

sb_fail:
	log_debug("Cleaning up after superblock mount failure.");
	kfree(g_vfs_root_mount->mount_point);
mount_point_fail:
	log_debug("Cleaning up after mount point allocation failure.");
	kfree(g_vfs_root_mount);
	g_vfs_root_mount = nullptr;
	return -1;
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
	// TODO: Make better hashtable implementation like the linux kernel.
	struct vfs_dentry* found;
	// Have to allocate some stuff beforehand for the filesystem and hash table checks
	struct vfs_dentry* child = slab_alloc(&dentry_cache);
	if (!child) return nullptr;
	child->name = strdup(name);
	if (!child->name) {
		slab_free(&dentry_cache, child);
		return nullptr;
	}
	child->parent = parent;
	child->fs_data = parent->fs_data;
	// Check hash table first if found exit
	if ((found = ht_get(dentry_ht, child))) {
		// Since we found it, we free all the child init stuff then return found
		kfree(child->name);
		slab_free(&dentry_cache, child);
		return dget(found);
	}

	if (!parent->inode || !parent->inode->ops || !parent->inode->ops->lookup) {
		log_error("Invalid inode operations");
		kfree(child->name);
		slab_free(&dentry_cache, child);
		return nullptr; // Handle invalid inode or missing lookup operation
	}

	// Can just return the end result, if not found then child is a negative
	// dentry (inode == NULL). Otherwise it was properly found.
	return parent->inode->ops->lookup(parent->inode, child);
}

/**
 * @brief Computes a 32-bit hash for a directory entry (dentry).
 *
 * Generates a hash value based on the parent inode ID and the dentry name,
 * using the FNV-1a algorithm. Handles NULL pointers safely.
 *
 * @param key Pointer to a `struct vfs_dentry` to be hashed.
 *            If NULL, returns 0. If parent/inode or name are NULL,
 *            special constants are mixed into the hash.
 * @return 32-bit FNV-1a hash value representing the dentry.
 */
uint32_t dentry_hash(const void* key)
{
	static constexpr u32 FNV_PRIME = 0x01000193;
	static constexpr u32 FNV_OFFSET = 0x811c9dc5;
	static constexpr unsigned int SENTINEL = 0xFF;

	if (!key) {
		return 0;
	}

	struct vfs_dentry* dkey = (struct vfs_dentry*)key;
	uint32_t hash = FNV_OFFSET;

	// Mix parent inode ID, or sentinel if not present
	if (!dkey->parent || !dkey->parent->inode) {
		for (size_t i = 0; i < sizeof(size_t); i++) {
			hash ^= SENTINEL;
			hash *= FNV_PRIME;
		}
	} else {
		size_t id = dkey->parent->inode->id;
		uint8_t* id_bytes = (uint8_t*)&id;
		for (size_t i = 0; i < sizeof(id); i++) {
			hash ^= id_bytes[i];
			hash *= FNV_PRIME;
		}
	}

	// Mix name bytes, or sentinel if not present
	if (!dkey->name) {
		hash ^= SENTINEL;
		hash *= FNV_PRIME;
	} else {
		for (const char* p = dkey->name; *p; p++) {
			hash ^= (uint32_t)(unsigned char)(*p);
			hash *= FNV_PRIME;
		}
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

#if 0

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
#endif
	UNIMPLEMENTED;
	return -1;
}

/**
 * vfs_close - Closes a file in the virtual filesystem.
 * @file: Pointer to the vfs_file structure representing the file to be closed.
 */
void vfs_close(struct vfs_file* file)
{
	UNIMPLEMENTED;
}

void vfs_dump_child(struct vfs_dentry* parent)
{
	struct vfs_dentry* child;
	list_for_each_entry(child, &parent->children, siblings)
	{
		log_debug("%s", child->name);
	}
}

int vfs_mkdir(const char* path, uint16_t mode)
{
	if (!path) {
		return -VFS_ERR_INVAL;
	}

	if (strcmp(path, "/") == 0) {
		return -VFS_ERR_EXIST;
	}

	char* buf = strdup(path);
	trim_trailing(buf, '/');
	char* name = strrchr(buf, '/');

	if (!name || !*(name + 1)) {
		kfree(buf);
		return -VFS_ERR_INVAL;
	}

	// Split into parent and name
	*name = '\0';
	name++; // Now, name points to the last component

	// Determine parent string
	const char* parent = (buf[0] == '\0') ? "/" : buf;

	log_debug("Path: %s, p: %s, name: %s", path, parent, name);

	struct vfs_dentry* pdentry = vfs_lookup(parent);
	if (!pdentry) {
		kfree(buf);
		return -VFS_ERR_NOENT;
	}

	struct vfs_inode* pinode = pdentry->inode;

	// Optionally, check for existing child with the same name
	if (vfs_does_name_exist(pdentry, name)) {
		kfree(buf);
		return -VFS_ERR_EXIST;
	}

	struct vfs_dentry* child = dentry_alloc(pdentry, name);
	if (!child) {
		kfree(buf);
		return -VFS_ERR_NOMEM;
	}

	int res = pinode->ops->mkdir(pinode, child, mode);
	if (res < 0) {
		kfree(child->name);
		slab_free(&dentry_cache, child);
		kfree(buf);
	}

	dentry_add(child);

	kfree(buf);
	return VFS_OK;
}

bool vfs_does_name_exist(struct vfs_dentry* parent, const char* name)
{
	struct vfs_dentry* child;
	list_for_each_entry(child, &parent->children, siblings)
	{
		if (!strcmp(child->name, name)) {
			return true;
		}
	}

	return false;
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
#if 0
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
#endif
}

// source should be nullptr for ramfs/virual fs types
int vfs_mount(const char* source, const char* target, const char* fstype, int flags)
{
	return -1;

	// 1. Find the filesystem type (e.g., "fat32", "ramfs") in your list of registered filesystems.
	struct vfs_fs_type* fs = find_filesystem(fstype);
	if (!fs) {
		return -ENODEV; // Filesystem not found
	}

	// 2. Find the directory in the VFS that we want to mount on.
	//    You'll need a path walking function for this (e.g., vfs_lookup(target)).
	struct vfs_dentry* mount_point_dentry = vfs_lookup(target);
	if (!mount_point_dentry) {
		return -ENOENT; // Mount point doesn't exist
	}
	// TODO: Add a check to ensure mount_point_dentry is a directory.

	// 3. Call the filesystem-specific mount function.
	//    This is where the magic happens! It returns a fully formed superblock.
	struct vfs_superblock* sb = fs->mount(source, flags);
	if (!sb) {
		return -EFAULT; // The FS failed to mount
	}

	// 4. "Graft" the new filesystem onto the mount point.
	//    The dentry for the mount point should now point to the new superblock's root.
	// mount_point_dentry->inode = sb->root_dentry;
	// You'll also want to link the mount information so you can unmount it later.
	// Your vfs_mount struct is good for this.

	log_info("Mounted %s on %s type %s", source, target, fstype);
	return 0; // Success!
}

struct vfs_dentry* vfs_lookup(const char* path)
{
	// If the VFS isn't even mounted, it's a fatal error.
	if (g_vfs_root_mount == nullptr) {
		panic("VFS lookup called before rootfs was mounted!");
	}

	// Is the path just "/"? Easy! Return the root dentry.
	if (path[0] == '/' && path[1] == '\0') {
		return g_vfs_root_mount->sb->root_dentry;
	}

	char* buf = strdup(path);
	trim_trailing(buf, '/');

	// For any other path like "/foo/bar", start the search
	// from the root dentry.
	struct vfs_dentry* current_dentry = g_vfs_root_mount->sb->root_dentry;

	current_dentry = vfs_walk_path(current_dentry, buf);

	kfree(buf);
	return current_dentry;
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
		return vfs_walk_path(g_vfs_root_mount->sb->root_dentry, path + 1);
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
	const char* token;
	size_t len = 0;
	struct path_tokenizer tok = { .path = path };
	struct vfs_dentry* parent = root;

	while ((token = path_next_token(&tok, &len))) {
		parent = dentry_lookup(parent, token);
		if (!parent) {
			return nullptr;
		}
	}

	return parent;
}

struct vfs_dentry* dentry_alloc(struct vfs_dentry* parent, const char* name)
{
	// Basic allocation
	struct vfs_dentry* dentry = slab_alloc(&dentry_cache);
	if (!dentry) return nullptr;

	dentry->name = strdup(name);
	if (!dentry->name) {
		slab_free(&dentry_cache, dentry);
		return nullptr;
	}

	dentry->parent = parent;

	dentry->inode = nullptr;
	dentry->ref_count = 1;
	dentry->flags = 0; // The caller can set flags like DENTRY_DIR later

	list_init(&dentry->children);
	list_init(&dentry->siblings);

	return dentry;
}

/*******************************************************************************
* Private Function Definitions
*******************************************************************************/

static struct vfs_fs_type* find_filesystem(const char* fs_type)
{
	struct vfs_fs_type* p = fs_list;
	while (p) {
		if (strncmp(fs_type, p->fs_type, FS_TYPE_LEN) == 0) return p;
		p = p->next;
	}
	return nullptr;
}

static void trim_trailing(char* s, char c)
{
	size_t i = strlen(s);
	while (i >= 0 && s[i - 1] == c) {
		i--;
	}

	s[i] = '\0';
}

static const char* path_next_token(struct path_tokenizer* tok, size_t* out_len)
{
	if (!tok || !tok->path) {
		return nullptr;
	}

	// Skip leading slashes
	while (tok->path[tok->offset] == '/') {
		tok->offset++;
	}

	// Check if end of string is reached
	if (tok->path[tok->offset] == '\0') {
		return nullptr;
	}

	const char* start = &tok->path[tok->offset];
	const char* end = strchr(start, '/');

	if (end) {
		*out_len = (size_t)(end - start);
	} else {
		*out_len = strlen(start);
	}

	tok->offset += *out_len;

	return start;
}
