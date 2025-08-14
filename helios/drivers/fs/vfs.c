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

#include <drivers/fs/devfs.h>
#include <drivers/fs/fat.h>
#include <drivers/fs/ramfs.h>
#include <drivers/fs/vfs.h>
#include <kernel/panic.h>
#include <kernel/tasks/scheduler.h>
#include <mm/slab.h>
#include <stdlib.h>
#include <string.h>
#include <util/hashtable.h>
#include <util/ht.h>
#include <util/log.h>

// TODO: Find a better way to handle some of these icky globals, also def need
// some locks

/*******************************************************************************
 * Global Variable Definitions
 *******************************************************************************/

struct vfs_fs_type* fs_list = NULL;
struct vfs_mount* mount_list = NULL;
struct vfs_superblock** sb_list;
static uint8_t sb_idx = 0;

struct slab_cache dentry_cache = { 0 };
struct slab_cache file_cache = { 0 };

static constexpr int d_ht_bits = 12; // 4096 buckets
DEFINE_HASHTABLE(d_ht, d_ht_bits);

static constexpr int i_ht_bits = 12; // 4096 buckets
DEFINE_HASHTABLE(i_ht, i_ht_bits);

static struct vfs_mount* g_vfs_root_mount = nullptr;

/**
 * Lightweight iterator over slash-delimited path segments.
 */
struct path_tokenizer {
	const char* path; ///< NUL-terminated input path string
			  ///< (not owned or modified).
	size_t offset;	  ///< Current byte offset into path for the next
			  ///< component.
};

/*******************************************************************************
 * Private Function Prototypes
 *******************************************************************************/

/**
 * trim_trailing - Remove trailing occurrences of a character from a string.
 * @s: Mutable, NUL-terminated C string buffer to trim (modified in-place).
 * @c: Character to remove from the end of @s.
 *
 * Scans backward from the end of @s and truncates all trailing @c by
 * writing a terminating NUL at the first non-@c position.
 */
static void trim_trailing(char* s, char c);
static const char* path_next_token(struct path_tokenizer* tok, size_t* out_len);
static struct vfs_fs_type* find_filesystem(const char* fs_type);
static void register_mount(struct vfs_mount* mnt);
static inline int vfs_create_args_valid(const char* path,
					uint16_t mode,
					int flags,
					struct vfs_dentry** out);

static void add_superblock(struct vfs_superblock* sb)
{
	if (sb_idx >= 8) return;
	sb_list[sb_idx++] = sb;
}

static inline u32 inode_key(const struct vfs_superblock* sb, const size_t id)
{
	return (u32)((uptr)sb ^ id);
}

/*******************************************************************************
 * Public Function Definitions
 *******************************************************************************/

/**
 * vfs_init - Initializes the virtual filesystem.
 */
void vfs_init()
{
	sb_list = kmalloc(sizeof(uintptr_t) * 8);

	int res = slab_cache_init(&dentry_cache,
				  "VFS Dentry",
				  sizeof(struct vfs_dentry),
				  0,
				  NULL,
				  NULL);
	if (res < 0) {
		log_error(
			"Could not init dentry cache, slab_cache_init() returned %d",
			res);
		panic("Dentry cache init failure");
	}

	res = slab_cache_init(&file_cache,
			      "VFS Filesystem",
			      sizeof(struct vfs_file),
			      8,
			      nullptr,
			      nullptr);
	if (res < 0) {
		log_error(
			"Could not init file cache, slab_cache_init() returned %d",
			res);
		panic("file cache init failure");
	}

	ramfs_init();
	devfs_init();

	mount_initial_rootfs();
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
	sb->mount_point = g_vfs_root_mount->mount_point;
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
 * @brief Creates a new, empty inode and adds it to the inode cache.
 *
 * This function is used when creating a new file or directory. It allocates
 * a blank inode, initializes its basic VFS fields (sb, id, ref_count), and
 * inserts it into the global inode hash table. It does NOT populate it
 * with filesystem-specific data; that is the caller's responsibility.
 *
 * @param sb The superblock of the filesystem where the new inode belongs.
 * @param id The unique ID for the new inode.
 * @return A pointer to the new, locked vfs_inode, or NULL on failure.
 */
struct vfs_inode* new_inode(struct vfs_superblock* sb, size_t id)
{
	if (inode_ht_check(sb, id)) {
		log_error(
			"Inode %zu already exists in cache, cannot create new.",
			id);
		return nullptr;
	}

	// Ask the filesystem to allocate memory for the inode structure.
	if (!sb->sops || !sb->sops->alloc_inode) {
		return nullptr;
	}
	struct vfs_inode* inode = sb->sops->alloc_inode(sb);
	if (!inode) {
		return nullptr;
	}

	// Initialize the core VFS fields
	inode->sb = sb;
	inode->id = id;
	inode->ref_count = 1;

	// Add it to the cache so future lookups will find it.
	inode_add(inode);

	return inode;
}

/**
 * @brief  Obtains an in-memory VFS inode from the global inode cache.
 *
 * This function is the primary way to get a pointer to an active `vfs_inode`.
 * It uniquely identifies an inode using its superblock and on-disk inode
 * number.
 *
 * The function first searches the global inode cache.
 * - If the inode is found (a cache hit), its reference count is incremented,
 * and a pointer to the existing in-memory inode is returned.
 * - If the inode is not found (a cache miss), a new `vfs_inode` is allocated,
 * and the filesystem-specific `read_inode` operation is called (via the
 * superblock) to populate it with data from the underlying storage. The new
 * inode is then added to the cache before being returned.
 *
 * @param sb    A pointer to the `vfs_superblock` of the filesystem where the
 * inode resides. This provides the context for the filesystem type
 * and its operations.
 * @param id    The inode number, which is a unique identifier for the inode
 * within its filesystem.
 *
 * @return      A pointer to the locked `vfs_inode` structure with an
 * incremented reference count. Returns NULL if allocation or
 * reading from disk fails.
 *
 * @note        Every successful call to `iget` must be paired with a
 * corresponding call to `iput` to release the reference when the inode is no
 * longer needed.
 */
struct vfs_inode* iget(struct vfs_superblock* sb, size_t id)
{
	struct vfs_inode* inode = inode_ht_check(sb, id);
	if (inode) {
		inode->ref_count++;
		return inode;
	}

	if (sb->sops == nullptr || sb->sops->alloc_inode == nullptr) {
		log_error("Superblock %p has no alloc_inode operation",
			  (void*)sb);
		return nullptr;
	}

	inode = sb->sops->alloc_inode(sb);

	if (!inode) {
		log_error("Failed to allocate inode for id %zu in sb %p",
			  id,
			  (void*)sb);
		return nullptr;
	}

	inode->sb = sb;
	inode->id = id;
	inode->ref_count = 1;

	if (sb->sops->read_inode(inode) < 0) {
		log_error("Failed to read inode %zu from superblock %p",
			  id,
			  (void*)sb);
		sb->sops->destroy_inode(inode);
		return nullptr;
	}

	inode_add(inode);

	return inode;
}

/**
 * @brief  Releases a reference to an in-memory VFS inode.
 *
 * This function decrements the `ref_count` of an inode. It is the counterpart
 * to `iget`. When the reference count drops to zero, it signifies that no part
 * of the kernel is actively using the inode.
 *
 * An inode with a zero reference count becomes a candidate for being written
 * back to disk if it is dirty (modified) and eventually being evicted from
 * the inode cache to reclaim memory, especially under memory pressure.
 *
 * @param inode A pointer to the `vfs_inode` whose reference should be released.
 * If the pointer is NULL, the function does nothing.
 *
 * @note        This function must be called to balance every call to `iget`
 * to prevent inode reference leaks, which would result in memory
 * leaks and prevent filesystems from being unmounted correctly.
 */
void iput(struct vfs_inode* inode)
{
	if (!inode) {
		return;
	}

	inode->ref_count--;
	log_debug("Inode %zu ref_count: %d", inode->id, inode->ref_count);

	if (inode->ref_count > 0) {
		return;
	}

	log_debug("Deallocating inode %zu", inode->id);
	hash_del(&inode->hash);
	if (inode->sb && inode->sb->sops && inode->sb->sops->destroy_inode) {
		inode->sb->sops->destroy_inode(inode);
	} else {
		kfree(inode);
	}
}

void inode_add(struct vfs_inode* inode)
{
	u32 key = inode_key(inode->sb, inode->id);
	struct hlist_head* bucket = &i_ht[hash_min(key, HASH_BITS(i_ht))];
	inode->bucket = bucket;

	// hash_add(i_ht, &inode->hash, key);
	hlist_add_head(bucket, &inode->hash);
}

/**
 * inode_ht_check - Search for an existing inode in the hash table
 * @sb: Superblock containing the filesystem instance
 * @id: Unique inode identifier within the filesystem
 *
 * Return: Pointer to the existing inode if found, nullptr otherwise
 */
struct vfs_inode* inode_ht_check(struct vfs_superblock* sb, size_t id)
{
	// Early parameter validation
	if (unlikely(!sb)) {
		return nullptr;
	}

	u32 key = inode_key(sb, id);
	struct vfs_inode* candidate;

	hash_for_each_possible (i_ht, candidate, hash, key) {
		// Compare inode ID first - most selective and cheapest comparison
		if (likely(candidate->id == id) && candidate->sb == sb) {
			return candidate;
		}
	}

	return nullptr;
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
	log_debug("Dentry %s ref_count: %d", dentry->name, dentry->ref_count);
	return dentry;
}

void dput(struct vfs_dentry* dentry)
{
	dentry->ref_count--;
	log_debug("Dentry %s ref_count: %d", dentry->name, dentry->ref_count);
	if (dentry->ref_count <= 0) {
		log_debug("Deallocating dentry %s", dentry->name);
		iput(dentry->inode);
		dentry_dealloc(dentry);
	}
}

/**
 * dentry_add - Adds a dentry to the hash table.
 * @dentry: Pointer to the vfs_dentry structure to be added.
 */
void dentry_add(struct vfs_dentry* dentry)
{
	u32 hash = dentry_hash(dentry);
	struct hlist_head* bucket = &d_ht[hash_min(hash, HASH_BITS(d_ht))];
	dentry->bucket = bucket;
	hash_add(d_ht, &dentry->hash, hash);
	dentry->inode->nlink++;
	dget(dentry);
}

/**
 * dentry_ht_check - Check if a dentry exists in the hash table.
 * @d: Pointer to the dentry to check.
 *
 * Return: Pointer to the matching dentry if found, or NULL if not found.
 */
struct vfs_dentry* dentry_ht_check(struct vfs_dentry* d)
{
	u32 key = dentry_hash(d);
	struct vfs_dentry* obj = nullptr;
	hash_for_each_possible (d_ht, obj, hash, key) {
		if (dentry_compare(d, obj)) {
			return obj;
		}
	}
	return nullptr;
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
	log_debug("dentry_lookup: parent=%p, name=%s", (void*)parent, name);

	struct vfs_dentry* found;
	struct vfs_dentry* child = dentry_alloc(parent, name);

	// Check hash table first
	if ((found = dentry_ht_check(child))) {
		// Since we found it, we free all the child init stuff then
		// return found
		log_debug("Found dentry %s in hash table", name);
		dentry_dealloc(child);
		return dget(found);
	}

	if (!parent->inode || !parent->inode->ops ||
	    !parent->inode->ops->lookup) {
		log_error("Invalid inode operations");
		dentry_dealloc(child);
		return nullptr; // Handle invalid inode or missing lookup
				// operation
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
u32 dentry_hash(const struct vfs_dentry* key)
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
 * @param d1 A pointer to the first `vfs_dentry` structure.
 * @param d2 A pointer to the second `vfs_dentry` structure.
 * @return `true` if the dentries are equal, `false` otherwise.
 */
bool dentry_compare(const struct vfs_dentry* d1, const struct vfs_dentry* d2)
{
	return (strcmp(d1->name, d2->name) == 0) &&
	       (d1->parent->inode->id == d2->parent->inode->id);
}

int vfs_open(const char* path, int flags)
{
	// TODO: Rework lookup to check for mount points.

	struct vfs_dentry* dentry = vfs_lookup(path);
	if (!dentry || !dentry->inode) {
		log_debug("Dentry not found for path: %s", path);
		if (flags & O_CREAT) {
			int res =
				vfs_create(path, VFS_PERM_ALL, flags, &dentry);
			if (res < 0) {
				return res;
			}
		} else {
			return -VFS_ERR_NOENT;
		}
	}

	// TODO: Validate flags (dentries don't support access flags yet)

	struct vfs_file* file = slab_alloc(&file_cache);
	if (!file) {
		// TODO: release dentry?
		log_error("Could not allocate vfs_file");
		return -VFS_ERR_NOMEM;
	}

	file->dentry = dget(dentry);
	file->f_pos = (flags & O_APPEND) ? (off_t)dentry->inode->f_size : 0;
	file->flags = flags;
	file->ref_count = 1;
	file->fops = dentry->inode->fops;

	if (file->fops->open) {
		int res = file->fops->open(dentry->inode, file);
		if (res < 0) {
			slab_free(&file_cache, file);
			return res;
		}
	}

	int fd = install_fd(get_current_task(), file);
	if (fd < 0) {
		slab_free(&file_cache, file);
		return -VFS_ERR_NOSPC; // Is this the right code?
	}

	return fd;
}

/**
 * vfs_close - Close a file descriptor
 * @fd: File descriptor to close
 *
 * Return: VFS_OK on success, -VFS_ERR_INVAL if the file descriptor is invalid
 */
int vfs_close(int fd)
{
	struct vfs_file* file = get_file(fd);
	if (!file) {
		return -VFS_ERR_INVAL;
	}

	file->ref_count--;
	log_debug("File %s ref_count: %d", file->dentry->name, file->ref_count);
	if (file->ref_count <= 0) {
		if (file->fops->close) {
			file->fops->close(file->dentry->inode, file);
		}
		dput(file->dentry);
		slab_free(&file_cache, file);
	}

	// Clear the entry in the task's resource table
	get_current_task()->resources[fd] = nullptr;

	return VFS_OK;
}

void vfs_dump_child(struct vfs_dentry* parent)
{
	struct vfs_dentry* child;
	list_for_each_entry (child, &parent->children, siblings) {
		struct vfs_superblock* sb = child->inode->sb;
		log_debug("%s - type: %d, sb: '%s'(%p)",
			  child->name,
			  child->inode->filetype,
			  sb->mount_point,
			  (void*)sb);
	}
}

int vfs_create(const char* path,
	       uint16_t mode,
	       int flags,
	       struct vfs_dentry** out_dentry)
{
	int arg_check = vfs_create_args_valid(path, mode, flags, out_dentry);
	if (arg_check < 0) {
		return arg_check;
	}

	// Split path into parent + basename
	char* path_copy = strdup(path);
	trim_trailing(path_copy, '/');
	char* name = strrchr(path_copy, '/');

	if (!name || !*(name + 1)) {
		kfree(path_copy);
		return -VFS_ERR_INVAL;
	}

	// Split into parent and name
	*name = '\0';
	name++; // Now, name points to the last component

	// Determine parent string
	const char* parent = (path_copy[0] == '\0') ? "/" : path_copy;

	log_debug("Path: %s, p: %s, name: %s", path, parent, name);

	struct vfs_dentry* pdentry = vfs_lookup(parent);
	if (!pdentry || !pdentry->inode ||
	    !(pdentry->inode->filetype == FILETYPE_DIR)) {
		kfree(path_copy);
		return -VFS_ERR_NOTDIR;
	}

	// Try to lookup the file by name
	struct vfs_dentry* child = dentry_lookup(pdentry, name);

	if (child && child->inode) {
		if (flags & O_EXCL) {
			dput(child);
			kfree(path_copy);
			return -VFS_ERR_EXIST;
		}
		// File exists but not O_EXCL — treat as success?
		*out_dentry = child;
		kfree(path_copy);
		return VFS_OK;
	}

	child = dentry_alloc(pdentry, name);
	if (!child) {
		kfree(path_copy);
		return -VFS_ERR_NOMEM;
	}

	if (!pdentry->inode->ops || !pdentry->inode->ops->create) {
		dentry_dealloc(child);
		kfree(path_copy);
		return -VFS_ERR_NODEV;
	}

	int res = pdentry->inode->ops->create(pdentry->inode,
					      child,
					      mode); // or default mode
	if (res < 0) {
		dentry_dealloc(child);
		kfree(path_copy);
		return res;
	}
	dentry_add(child); // Now track the new dentry in the hashtable

	*out_dentry = child;

	return VFS_OK;
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
		dentry_dealloc(child);
		kfree(buf);
		return res;
	}

	dentry_add(child);

	kfree(buf);
	return VFS_OK;
}

ssize_t vfs_write(int fd, const char* buffer, size_t count)
{
	struct vfs_file* file = get_file(fd);
	if (!file) {
		return -VFS_ERR_INVAL;
	}

	return file->fops->write(file, buffer, count);
}

ssize_t vfs_read(int fd, char* buffer, size_t count)
{
	struct vfs_file* file = get_file(fd);
	if (!file) {
		return -VFS_ERR_INVAL;
	}

	return file->fops->read(file, buffer, count);
}

off_t vfs_lseek(int fd, off_t offset, int whence)
{
	struct vfs_file* file = get_file(fd);
	if (!file) {
		return -VFS_ERR_INVAL;
	}

	switch (whence) {
	case SEEK_SET:
		file->f_pos = offset;
		return file->f_pos;
	case SEEK_CUR:
		if (file->f_pos + offset < 0) break;
		file->f_pos += offset;
		return file->f_pos;
	case SEEK_END:
		file->f_pos = (off_t)file->dentry->inode->f_size + offset;
		return file->f_pos;
	}

	return -VFS_ERR_INVAL;
}

struct vfs_file* get_file(int fd)
{
	if (fd >= MAX_RESOURCES || fd < 0) {
		return nullptr;
	}

	struct task* current_task = get_current_task();

	return current_task->resources[fd];
}

bool vfs_does_name_exist(struct vfs_dentry* parent, const char* name)
{
	struct vfs_dentry* child;
	list_for_each_entry (child, &parent->children, siblings) {
		if (!strcmp(child->name, name)) {
			return true;
		}
	}

	return false;
}

/**
 * @param source device to mount at (`/dev/sda1`). Can be nullptr for
 * ramfs/virtual devices
 * @param target path to mount at
 * @param fstype filesystem to mount
 * @param flags mount flags
 */
int vfs_mount(const char* source,
	      const char* target,
	      const char* fstype,
	      int flags)
{
	// 1. Find the filesystem type (e.g., "fat32", "ramfs") in your list of
	// registered filesystems.
	struct vfs_fs_type* fs = find_filesystem(fstype);
	if (!fs) {
		return -VFS_ERR_NODEV; // Filesystem not found
	}

	// 2. Find the directory in the VFS that we want to mount on.
	//    You'll need a path walking function for this (e.g.,
	//    vfs_lookup(target)).
	struct vfs_dentry* mount_point_dentry = vfs_lookup(target);
	if (!mount_point_dentry) {
		return -VFS_ERR_NOENT; // Mount point doesn't exist
	}
	// TODO: Add a check to ensure mount_point_dentry is a directory.

	// 3. Call the filesystem-specific mount function.
	//    This is where the magic happens! It returns a fully formed
	//    superblock.
	struct vfs_superblock* sb = fs->mount(source, flags);
	if (!sb) {
		return -VFS_ERR_NODEV; // The FS failed to mount
	}

	// 4. "Graft" the new filesystem onto the mount point.
	//    The dentry for the mount point should now point to the new
	//    superblock's root.
	mount_point_dentry->inode = sb->root_dentry->inode;
	// You'll also want to link the mount information so you can unmount it
	// later. Your vfs_mount struct is good for this.
	struct vfs_mount* new_mount =
		(struct vfs_mount*)kmalloc(sizeof(struct vfs_mount));
	new_mount->mount_point = strdup(target);
	sb->mount_point = new_mount->mount_point;
	new_mount->sb = sb;
	new_mount->flags = flags;
	register_mount(new_mount);

	log_info("Mounted %s on %s type %s", source, target, fstype);
	return VFS_OK; // Success!
}

struct vfs_dentry* vfs_lookup(const char* path)
{
	// If the VFS isn't even mounted, it's a fatal error.
	if (g_vfs_root_mount == nullptr) {
		panic("VFS lookup called before rootfs was mounted!");
	}

	// Is the path just "/"? Easy! Return the root dentry.
	if (path[0] == '/' && path[1] == '\0') {
		return dget(g_vfs_root_mount->sb->root_dentry);
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
		if (strncmp(path, m->mount_point, len) == 0 &&
		    (path[len] == '/' || path[len] == '\0')) {
			if (len > match_len) {
				match = m;
				match_len = len;
			}
		}
	}

	if (!match) {
		// FIXME: Doesn't varify rootfs->root_dentry exists
		return vfs_walk_path(g_vfs_root_mount->sb->root_dentry,
				     path + 1);
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
		char token_buf[len + 1];
		memcpy(token_buf, token, len);
		token_buf[len] = '\0';
		log_debug("%s", token_buf);
		parent = dentry_lookup(parent, token_buf);
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
	dentry->ref_count = 0;
	dentry->flags = 0; // The caller can set flags like DENTRY_DIR later

	list_init(&dentry->children);
	list_init(&dentry->siblings);

	INIT_HLIST_NODE(&dentry->hash);

	return dentry;
}

void dentry_dealloc(struct vfs_dentry* d)
{
	// TODO: deallocate the inode attached to this dentry
	if (!list_empty(&d->children)) {
		log_warn("dentry still has children!");
		return;
	}

	if (!list_empty(&d->siblings)) {
		list_del(&d->siblings);
	}

	// hlist_del_init checks for hashed state before removing
	hash_del(&d->hash);

	if (d->name) {
		kfree(d->name);
	}

	if (d->inode) {
		d->inode->nlink--;
	}

	slab_free(&dentry_cache, d);
}

void test_tokenizer()
{

	size_t len = 0;
	const char* path = "/foo/bar/baz/qux";
	struct path_tokenizer tok = { .path = path };
	const char* token;

	log_info(TESTING_HEADER, "Path Tokenizer");

	log_debug("Testing path tokenizer with path: %s", path);

	while ((token = path_next_token(&tok, &len))) {
		char token_buf[len + 1];
		memcpy(token_buf, token, len);
		token_buf[len] = '\0';
		log_debug("Token: '%s', len: %zu", token_buf, len);
	}

	log_info(TESTING_FOOTER, "Path Tokenizer");
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

/**
 * trim_trailing - Remove trailing occurrences of a character from a string.
 * @s: Mutable, NUL-terminated C string buffer to trim (modified in-place).
 * @c: Character to remove from the end of @s.
 *
 * Scans backward from the end of @s and truncates all trailing @c by
 * writing a terminating NUL at the first non-@c position.
 */
static void trim_trailing(char* s, char c)
{
	if (!s || !*s) {
		return; // Nothing to trim
	}

	ssize_t i = (ssize_t)strlen(s);
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

	size_t start_offset = tok->offset;
	// const char* start = &tok->path[tok->offset];
	const char* token_start = &tok->path[tok->offset];

	// Scan forward to find the end of the current token
	while (tok->path[tok->offset] != '/' &&
	       tok->path[tok->offset] != '\0') {
		tok->offset++;
	}

	*out_len = tok->offset - start_offset;

	return token_start;
}

// TODO: Finish implementing
static inline int vfs_create_args_valid(const char* path,
					uint16_t mode,
					int flags,
					struct vfs_dentry** out)
{
	// Early validation of non-path parameters
	if (!out) {
		return -VFS_ERR_INVAL;
	}

	static constexpr int FORBIDDEN_FLAG_MASK = O_TRUNC | O_APPEND |
						   O_DIRECTORY;
	if (flags & FORBIDDEN_FLAG_MASK) {
		return -VFS_ERR_INVAL;
	}

	if ((mode & VFS_PERM_ALL) != mode) {
		return -VFS_ERR_INVAL;
	}

	// Single-pass path validation
	// NOTE: We only allow absolute paths for now
	if (!path || *path != '/') {
		return -VFS_ERR_INVAL;
	}

	// Skip leading slashes and validate path in one pass
	const char* p = path;
	while (*p == '/') {
		p++;
	}

	// Check for any path that's only slashes (including root "/")
	if (*p == '\0') {
		return -VFS_ERR_INVAL; // Path contains only slashes
	}

	// Validate path length while checking for valid characters
	size_t len = (size_t)(p - path); // Length of leading slashes
	while (*p && len < VFS_MAX_PATH) {
		p++;
		len++;
	}

	if (len >= VFS_MAX_PATH) {
		return -VFS_ERR_INVAL;
	}

	return VFS_OK;
}
