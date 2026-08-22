/**
 * @file drivers/fs/vfs.c
 *
 * Copyright (C) 2026 Dylan Parks
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

#include <uapi/helios/errno.h>

#undef LOG_LEVEL
#define LOG_LEVEL 1
#define FORCE_LOG_REDEF
#include "lib/log.h"
#undef FORCE_LOG_REDEF

#include "fs/devfs/devfs.h"
#include "fs/ramfs/ramfs.h"
#include "fs/vfs.h"
#include "kernel/assert.h"
#include "kernel/panic.h"
#include "kernel/tasks/scheduler.h"
#include "lib/hashtable.h"
#include "lib/string.h"
#include "mm/kmalloc.h"
#include "mm/slab.h"

/**
 * @addtogroup fs
 * @{
 */

// TODO: Find a better way to handle some of these icky globals, also def need
// some locks

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
 * @brief Lightweight iterator over slash-delimited path segments.
 */
struct path_tokenizer {
	const char* path; ///< NUL-terminated input path string
			  ///< (not owned or modified).
	size_t offset;	  ///< Current byte offset into path for the next
			  ///< component.
};

/**
 * @brief One path component, as a slice into another string.
 */
struct path_component {
	const char* start; /**< Points into the original string. Not NUL-terminated. */
	size_t len;	   /**< Length of the component. */
};

/**
 * @brief Adds a superblock to the global list of mounted superblocks.
 *
 * @param sb Superblock to add.
 */
static void add_superblock(struct vfs_superblock* sb)
{
	if (sb_idx >= 8) return;
	sb_list[sb_idx++] = sb;
}

/**
 * @brief Computes the inode hash table key for an (sb, id) pair.
 *
 * @param sb Superblock that owns the inode.
 * @param id Inode ID within that superblock.
 *
 * @return The hash key.
 */
static inline u32 inode_key(const struct vfs_superblock* sb, const size_t id)
{
	return (u32)((uptr)sb ^ id);
}

/**
 * @brief Initializes the virtual filesystem.
 */
void vfs_init()
{
	sb_list = (struct vfs_superblock**)kmalloc(sizeof(*sb_list) * 8);

	int res = slab_cache_init(&dentry_cache, "VFS Dentry", sizeof(struct vfs_dentry), 0, NULL, NULL);
	if (res < 0) {
		log_error("Could not init dentry cache, slab_cache_init() returned %d", res);
		panic("Dentry cache init failure");
	}

	res = slab_cache_init(&file_cache, "VFS Filesystem", sizeof(struct vfs_file), 8, nullptr, nullptr);
	if (res < 0) {
		log_error("Could not init file cache, slab_cache_init() returned %d", res);
		panic("file cache init failure");
	}

	// TODO: Better way to init supported filesystems
	ramfs_init();
	devfs_init();

	mount_initial_rootfs();
}

/**
 * @brief Registers a mount point in the virtual filesystem.
 *
 * @param mnt Pointer to the vfs_mount structure for the mount point.
 *
 * Adds the mount point to the head of the global mount list.
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
 * @brief Registers a filesystem type in the virtual filesystem.
 *
 * @param fs Pointer to the vfs_fs_type structure for the filesystem type.
 *
 * Adds the filesystem type to the head of the global filesystem list.
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

/**
 * @brief Mounts ramfs at "/" as the initial root filesystem.
 *
 * @return 0 on success, or -1 on failure.
 *
 * @note Called once during vfs_init(), before any other mount exists.
 */
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
 * @param sb Superblock of the filesystem that owns the new inode.
 * @param id Unique ID for the new inode.
 *
 * @return A pointer to the new vfs_inode, or NULL on failure.
 */
struct vfs_inode* new_inode(struct vfs_superblock* sb, size_t id)
{
	if (inode_ht_check(sb, id)) {
		log_error("Inode %zu already exists in cache, cannot create new.", id);
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
	inode->ref_count = 1; // Ref held by caller

	// Add it to the cache so future lookups will find it.
	inode_add(inode);

	return inode;
}

/**
 * @brief Acquires a counted reference to an inode.
 *
 * @param inode Inode to reference. May be nullptr.
 *
 * @return The same inode, or nullptr if inode was nullptr.
 */
struct vfs_inode* iget(struct vfs_inode* inode)
{
	if (inode) {
		inode->ref_count++;
	}

	return inode;
}

/**
 * @brief Releases a reference to an in-memory VFS inode.
 *
 * @param inode Pointer to the vfs_inode whose reference is released.
 *
 * Frees the inode when its reference count reaches zero.
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

/**
 * @brief Adds an inode to the global inode hash table.
 *
 * @param inode Inode to add to the hash table.
 *
 * Takes a reference on the inode.
 */
void inode_add(struct vfs_inode* inode)
{
	u32 key = inode_key(inode->sb, inode->id);
	struct hlist_head* bucket = &i_ht[hash_min(key, HASH_BITS(i_ht))];
	inode->bucket = bucket;

	iget(inode);
	hlist_add_head(bucket, &inode->hash);
}

/**
 * @brief Searches for an existing inode in the hash table.
 *
 * @param sb Superblock containing the filesystem instance.
 * @param id Unique inode identifier within the filesystem.
 *
 * @return Pointer to the existing inode if found, or nullptr otherwise.
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
 * @brief Acquires a counted reference to a dentry.
 *
 * @param dentry Dentry to reference. May be NULL.
 *
 * @return The same dentry, or NULL if dentry was NULL.
 *
 * Use dget() to return an existing cached dentry, or to store a dentry in a
 * structure that outlives the current scope. Do not call dget() on a
 * freshly allocated dentry from dentry_alloc(); it already has a refcount
 * of one.
 */
struct vfs_dentry* dget(struct vfs_dentry* dentry)
{
	if (!dentry) {
		return dentry;
	}

	dentry->ref_count++;
	log_debug("Dentry '%s' ref_count: %d", dentry->name, dentry->ref_count);

	return dentry;
}

/**
 * @brief Releases a counted reference to a dentry.
 *
 * @param dentry Dentry to release. May be NULL.
 *
 * Decrements the reference count. At zero, releases the inode and frees the
 * dentry.
 *
 * @note NULL-safe. After the call, do not dereference dentry unless
 * another reference is held elsewhere.
 */
void dput(struct vfs_dentry* dentry)
{
	if (!dentry) {
		return;
	}

	dentry->ref_count--;
	log_debug("Dentry %s ref_count: %d", dentry->name, dentry->ref_count);
	if (dentry->ref_count <= 0) {
		log_debug("Deallocating dentry %s", dentry->name);
		iput(dentry->inode);
		dentry_dealloc(dentry);
	}
}

/**
 * @brief Adds a dentry to the hash table.
 *
 * @param dentry Pointer to the vfs_dentry to add.
 */
void dentry_add(struct vfs_dentry* dentry)
{
	u32 hash = dentry_hash(dentry);
	struct hlist_head* bucket = &d_ht[hash_min(hash, HASH_BITS(d_ht))];
	dentry->bucket = bucket;
	hash_add(d_ht, &dentry->hash, hash);
	dget(dentry);
	log_debug("Added dentry %s to hash table, ref_count: %d", dentry->name, dentry->ref_count);
}

/**
 * @brief Checks whether a dentry exists in the hash table.
 *
 * @param d Pointer to the dentry to check.
 *
 * @return Pointer to the matching dentry if found, or NULL otherwise.
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
 * @brief Finds or constructs a child dentry under a parent directory.
 *
 * @param parent Directory dentry to search.
 * @param name Child name.
 *
 * @return A referenced dentry on success. The caller must call dput(). NULL
 * on error.
 *
 * On a cache hit, returns an extra reference on the existing dentry. On a
 * miss, calls the filesystem's lookup() operation, which must populate and
 * return the freshly allocated child dentry, or take a reference on a
 * different dentry it returns instead.
 */
struct vfs_dentry* __dentry_lookup(struct vfs_dentry* parent, const char* name)
{
	log_debug("dentry_lookup: parent=%s, name=%s", parent->name, name);

	struct vfs_dentry* found;
	struct vfs_dentry* child = dentry_alloc(parent, name);

	// Check hash table first
	found = dentry_ht_check(child);
	if (found) {
		// Since we found it, we free all the child init stuff then
		// return found
		log_debug("Found dentry %s in hash table", name);
		dentry_dealloc(child);
		return dget(found);
	}

	if (!parent->inode || !parent->inode->ops || !parent->inode->ops->lookup) {
		log_error("Invalid inode operations");
		dentry_dealloc(child);
		return nullptr; // Handle invalid inode or missing lookup
				// operation
	}

	// If we haven't found it above, it must be on disk or not exist.
	// So we query the filesystem via the parent inode's lookup op.
	return parent->inode->ops->lookup(parent->inode, child);
}

/**
 * @brief Computes a 32-bit hash for a dentry.
 *
 * @param key Pointer to the vfs_dentry to hash. Returns 0 if NULL.
 *
 * @return 32-bit FNV-1a hash value for the dentry.
 *
 * @note Handles a NULL parent, inode, or name by mixing in a sentinel
 * value.
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
 * @brief Compares two dentries for equality.
 *
 * @param d1 Pointer to the first vfs_dentry.
 * @param d2 Pointer to the second vfs_dentry.
 *
 * @return true if the dentries are equal, false otherwise.
 */
bool dentry_compare(const struct vfs_dentry* d1, const struct vfs_dentry* d2)
{
	return (strcmp(d1->name, d2->name) == 0) && (d1->parent->inode->id == d2->parent->inode->id);
}

/**
 * @brief Populates a VFS dirent from a dentry.
 *
 * @param dentry Dentry to copy the inode, name, and type from. Must not be
 * freed during the call.
 * @param dirent Output record to fill.
 *
 * @return 0 on success.
 *
 * @note Does not set dirent->d_off. The caller assigns the resume position.
 */
int __fill_dirent(struct vfs_dentry* dentry, struct dirent* dirent)
{
	dirent->d_ino = dentry->inode->id;

	switch (dentry->inode->filetype) {
	case FILETYPE_DIR:	dirent->d_type = DT_DIR; break;
	case FILETYPE_FILE:	dirent->d_type = DT_REG; break;
	case FILETYPE_CHAR_DEV: dirent->d_type = DT_CHR; break;
	default:		dirent->d_type = DT_UNKNOWN;
	}

	// For now we everything is static sized
	dirent->d_reclen = sizeof(struct dirent);

	strncpy(dirent->d_name, dentry->name, 255);
	dirent->d_name[255] = '\0';

	return 0;
}

/**
 * @brief Iterates a directory one entry at a time.
 *
 * @param dir Open directory file. Must reference a directory inode.
 * @param out Output dirent to fill.
 * @param pos Position cookie: 0 is ".", 1 is "..", and 2 or more are
 * children.
 *
 * @return 1 if an entry was emitted, 0 at the end of the directory, or a
 * negative errno on error.
 *
 * @note Holds the directory inode's read lock for the duration of the
 * call. Under concurrent mutation, an iterator may skip or repeat entries
 * but never returns partially initialized data.
 *
 * See: docs/man9/readdir.9.md
 */
int vfs_readdir(struct vfs_file* dir, struct dirent* out, long pos)
{
	if (pos == DIRENT_GET_NEXT) {
		pos = dir->f_pos;
	}

	if (!dir || !out || pos < 0) {
		return -EINVAL;
	}

	if (dir->dentry->inode->filetype != FILETYPE_DIR) {
		return -ENOTDIR;
	}

	if (dir->fops->readdir == nullptr) {
		return -ENOSYS;
	}

	int ret_val = 1;
	struct vfs_dentry* pdentry = dir->dentry;

	sem_wait(&pdentry->inode->lock);

	switch (pos) {
	case 0: {
		__fill_dirent(pdentry, out);
		strncpy(out->d_name, ".", 255);
		out->d_off = 1;
		dir->f_pos = 1;
		break;
	}
	case 1: {
		struct vfs_dentry* ppdentry = pdentry->parent;
		if (!ppdentry) {
			ppdentry = pdentry; // Root dir case
		}
		__fill_dirent(ppdentry, out);
		strncpy(out->d_name, "..", 255);
		out->d_off = 2;
		dir->f_pos = 2;
		break;
	}
	default: {
		int res = dir->fops->readdir(dir, out, pos - 2);
		if (res <= 0) {
			// Error or end of directory
			ret_val = res;
			goto ret;
		}

		// Set offset to next entry
		out->d_off += 1;
		dir->f_pos = out->d_off;
		break;
	}
	}

ret:
	sem_signal(&pdentry->inode->lock);
	return ret_val;
}

/**
 * @brief Reads multiple directory entries into a buffer.
 *
 * @param dir Open directory file.
 * @param dirp Output buffer of dirents.
 * @param count Size of dirp in bytes.
 *
 * @return Number of bytes written, 0 at the end of the directory, or a
 * negative errno on error.
 *
 * See: docs/man2/getdents.2.md
 */
ssize_t vfs_getdents(struct vfs_file* dir, struct dirent* dirp, size_t count)
{
	size_t num_dirp = count / sizeof(struct dirent);
	for (size_t i = 0; i < num_dirp; i++) {
		int res = vfs_readdir(dir, &dirp[i], DIRENT_GET_NEXT);
		if (res < 0) {
			return (ssize_t)res;
		} else if (res == 0) {
			// End of directory
			return (ssize_t)(i * sizeof(struct dirent));
		}
	}

	return (ssize_t)(num_dirp * sizeof(struct dirent));
}

/**
 * @brief Opens a file for a specific task.
 *
 * @param t Task to open the file for.
 * @param path Path to the file to open.
 * @param flags Open flags, for example O_RDONLY, O_WRONLY, or O_CREAT.
 *
 * @return File descriptor on success, or a negative errno on error.
 */
int __vfs_open_for_task(struct task* t, const char* path, int flags)
{
	char* norm_path = vfs_normalize_path(path, t->cwd);
	struct vfs_dentry* dentry = vfs_lookup(norm_path);
	if (!dentry || !dentry->inode) {
		log_debug("Dentry not found for path: %s", path);
		if (flags & O_CREAT) {
			int res = vfs_create(norm_path, VFS_PERM_ALL, flags, &dentry);
			if (res < 0 || !dentry || !dentry->inode) {
				kfree(norm_path);
				return res;
			}
		} else {
			kfree(norm_path);
			return -ENOENT;
		}
	}

	// TODO: Validate flags (dentries don't support access flags yet)

	struct vfs_file* file = slab_alloc(&file_cache);
	if (!file) {
		log_error("Could not allocate vfs_file");
		dput(dentry);
		kfree(norm_path);
		return -ENOMEM;
	}

	file->dentry = dentry;
	file->f_pos = (flags & O_APPEND) ? (off_t)dentry->inode->f_size : 0;
	file->flags = flags;
	file->ref_count = 1;
	file->fops = dentry->inode->fops;

	if (file->fops->open) {
		int res = file->fops->open(dentry->inode, file);
		if (res < 0) {
			dput(dentry);
			slab_free(&file_cache, file);
			kfree(norm_path);
			return res;
		}
	}

	int fd = install_fd(t, file);
	if (fd < 0) {
		dput(dentry);
		slab_free(&file_cache, file);
		kfree(norm_path);
		return -EMFILE; // Is this the right code?
	}
	log_debug("Opened file %s with fd %d and dref_count %d", dentry->name, fd, dentry->ref_count);

	kfree(norm_path);
	return fd;
}

/**
 * @brief Opens a file and returns a file descriptor.
 *
 * @param path Path to the file to open.
 * @param flags Open flags, for example O_RDONLY, O_WRONLY, or O_CREAT.
 */
int vfs_open(const char* path, int flags)
{
	return __vfs_open_for_task(get_current_task(), path, flags);
}

/**
 * @brief Closes a file descriptor.
 *
 * @param fd File descriptor to close.
 *
 * @return 0 on success, or -EINVAL if the file descriptor is invalid.
 */
int vfs_close(int fd)
{
	struct vfs_file* file = get_file(fd);
	if (!file) {
		return -EINVAL;
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

	return 0;
}

/**
 * @brief Checks whether a path exists.
 *
 * @param path Path to check.
 * @param amode Access mode to check. Currently ignored.
 *
 * @return 0 if path exists, or -ENOENT if it does not.
 *
 * @note Does not yet check amode against the inode's permissions.
 */
int vfs_access(const char* path, int amode)
{
	// Just going to skip amode for now :)
	(void)amode;

	log_info("path=%s, amode=%d", path, amode);
	struct vfs_dentry* dentry = vfs_lookup(path);
	if (!dentry || !dentry->inode) {
		return -ENOENT;
	}

	// TODO: Check permissions

	dput(dentry);
	return 0;
}

/**
 * @brief Adds a dentry to its parent's list of children.
 *
 * @param parent Parent directory dentry. May be nullptr.
 * @param child Child dentry to register. May be nullptr.
 */
void register_child(struct vfs_dentry* parent, struct vfs_dentry* child)
{
	if (!parent || !child) {
		return;
	}

	list_add_tail(&parent->children, &child->siblings);
}

/**
 * @brief Logs every child of a dentry. For debugging.
 *
 * @param parent Directory dentry to dump.
 */
void vfs_dump_child(struct vfs_dentry* parent)
{
	struct vfs_dentry* child;
	list_for_each_entry (child, &parent->children, siblings) {
		struct vfs_superblock* sb = child->inode->sb;
		log_debug("%s - type: %d, ref_count: %d, sb: '%s'(%p)",
			  child->name,
			  child->inode->filetype,
			  child->ref_count,
			  sb->mount_point,
			  (void*)sb);
	}
}

/**
 * @brief Splits a path into a parent directory and basename.
 *
 * @param path Input path string.
 * @param parent_out Receives an allocated parent string, or nullptr on
 * failure.
 * @param name_out Receives an allocated basename string, or nullptr on
 * failure.
 *
 * @return 0 on success, or a negative errno on error.
 *
 * @note May sleep. Allocates memory. Holds no locks.
 *
 * See: docs/man9/__split_string.9.md
 */
static int __split_path(const char* path, char** parent_out, char** name_out)
{
	// TODO: Expect a normalized path, so we can just tokenize on '/'
	if (!path || !parent_out || !name_out) {
		return -EINVAL;
	}

	const char* parent_begin;
	const char* name_begin;
	size_t parent_len;
	size_t name_len;
	size_t name_last;

	size_t path_len = strlen(path);
	if (path_len == 0) {
		*parent_out = *name_out = nullptr;
		return -EINVAL;
	}

	ssize_t scan = (ssize_t)path_len - 1;

	// After this loop, scan points to last non-'/' character,
	// or is < 0 if there is only slashes
	while (scan >= 0 && path[scan] == '/') {
		scan--;
	}

	if (scan < 0) {
		log_error("All slashes: '%s'", path);
		*parent_out = *name_out = nullptr;
		return -EINVAL;
	}

	name_last = (size_t)scan;

	// After this loop, scan points to the slash immediately before the basename,
	// or -1 if there is no parent slice.
	while (scan >= 0 && path[scan] != '/') {
		scan--;
	}

	name_len = name_last - (size_t)scan;
	name_begin = &path[scan + 1];

	if (name_len > VFS_MAX_NAME) {
		log_error("Name too long: '%s'", name_begin);
		*parent_out = *name_out = nullptr;
		return -ENAMETOOLONG;
	}

	// After this loop, scan points to final char of parent,
	// or -1 if there is no parent slice
	while (scan >= 0 && path[scan] == '/') {
		scan--;
	}

	if (scan < 0) {
		// parent is either '/' or '.'
		parent_begin = path[0] == '/' ? "/" : ".";
		parent_len = 1;
	} else {
		// Parent is valid
		parent_begin = path;
		parent_len = (size_t)scan + 1;
	}

	// Name being "." or ".." is usually invalid (especially for creation)
	if (name_begin[0] == '.' && (name_begin[1] == '.' || name_begin[1] == '\0')) {
		log_error("Invalid basename: '%s'", name_begin);
		*parent_out = *name_out = nullptr;
		return -EINVAL;
	}

	*parent_out = kzalloc(parent_len + 1);
	*name_out = kzalloc(name_len + 1);
	if (!*parent_out || !*name_out) {
		log_error("Could not allocate buffer");
		if (*parent_out) {
			kfree(*parent_out);
			*parent_out = nullptr;
		}
		if (*name_out) {
			kfree(*name_out);
			*name_out = nullptr;
		}
		return -ENOMEM;
	}

	memcpy(*parent_out, parent_begin, parent_len);
	memcpy(*name_out, name_begin, name_len);

	return 0;
}

// TODO: Finish implementing
/**
 * @brief Validates the arguments to vfs_create().
 *
 * @param path Absolute path to the file to create.
 * @param mode Permission bits to create the file with.
 * @param flags Open flags. O_TRUNC, O_APPEND, and O_DIRECTORY are rejected.
 * @param out Where vfs_create() will store the resulting dentry.
 *
 * @return 0 if the arguments are valid, or a negative errno otherwise.
 */
static inline int vfs_create_args_valid(const char* path, uint16_t mode, int flags, struct vfs_dentry** out)
{
	// Early validation of non-path parameters
	if (!out) {
		return -EINVAL;
	}

	static constexpr int FORBIDDEN_FLAG_MASK = O_TRUNC | O_APPEND | O_DIRECTORY;
	if (flags & FORBIDDEN_FLAG_MASK) {
		return -EINVAL;
	}

	if ((mode & VFS_PERM_ALL) != mode) {
		return -EINVAL;
	}

	// Single-pass path validation
	// NOTE: We only allow absolute paths for now
	if (!path || *path != '/') {
		return -EINVAL;
	}

	// Skip leading slashes and validate path in one pass
	const char* p = path;
	while (*p == '/') {
		p++;
	}

	// Check for any path that's only slashes (including root "/")
	if (*p == '\0') {
		return -EINVAL; // Path contains only slashes
	}

	// Validate path length while checking for valid characters
	size_t len = (size_t)(p - path); // Length of leading slashes
	while (*p && len < VFS_MAX_PATH) {
		p++;
		len++;
	}

	if (len >= VFS_MAX_PATH) {
		return -EINVAL;
	}

	return 0;
}

/**
 * @brief Creates a new file.
 *
 * @param path Path to the file to create. Resolved against the current
 * task's working directory if relative.
 * @param mode Permission bits for the new file.
 * @param flags Open flags. O_EXCL fails if the file already exists.
 * @param out_dentry Receives the file's dentry on success.
 *
 * @return 0 on success, or a negative errno on error.
 *
 * @note If the file already exists and O_EXCL is not set, this
 * succeeds and returns the existing dentry.
 */
int vfs_create(const char* path, uint16_t mode, int flags, struct vfs_dentry** out_dentry)
{
	if (!path) {
		return -EINVAL;
	}

	char* norm_path = vfs_normalize_path(path, get_current_task()->cwd);
	if (!norm_path) {
		return -ENOMEM;
	}

	int arg_check = vfs_create_args_valid(path, mode, flags, out_dentry);
	if (arg_check < 0) {
		return arg_check;
	}

	char* parent;
	char* name;
	int res = __split_path(norm_path, &parent, &name);

	if (res < 0 || !parent || !name) {
		kfree(norm_path);
		if (parent) kfree(parent);
		if (name) kfree(name);
		return res;
	}

	struct vfs_dentry* pdentry = vfs_lookup(parent);
	if (!pdentry || !pdentry->inode || !(pdentry->inode->filetype == FILETYPE_DIR)) {
		res = -ENOTDIR;
		goto free_all;
		// dput(pdentry);
		// kfree(norm_path);
		// kfree(parent);
		// kfree(name);
		// return -ENOTDIR;
	}

	// Try to lookup the file by name
	struct vfs_dentry* child = __dentry_lookup(pdentry, name);

	if (child && child->inode) {
		log_debug("child: %p, name: %p", child->name, name);
		if (flags & O_EXCL) {
			res = -EEXIST;
			dput(child);
			goto free_all;
			// kfree(norm_path);
			// kfree(parent);
			// kfree(name);
			// return -EEXIST;
		}
		// File exists but not O_EXCL — treat as success?
		*out_dentry = child;
		res = 0;
		goto free_all;
		// dput(pdentry);
		// kfree(norm_path);
		// kfree(parent);
		// kfree(name);
		// return 0;
	}

	child = dentry_alloc(pdentry, name);
	log_debug("child: %p, name: %p", child->name, name);
	if (!child) {
		res = -ENOMEM;
		goto free_all;
		// dput(pdentry);
		// kfree(norm_path);
		// kfree(parent);
		// kfree(name);
		// return -ENOMEM;
	}

	if (!pdentry->inode->ops || !pdentry->inode->ops->create) {
		res = -ENODEV;
		dentry_dealloc(child);
		goto free_all;
		// dput(pdentry);
		// kfree(norm_path);
		// kfree(parent);
		// kfree(name);
		// return -ENODEV;
	}

	res = pdentry->inode->ops->create(pdentry->inode, child, mode);
	if (res < 0) {
		dentry_dealloc(child);
		goto free_all;
		// dput(pdentry);
		// kfree(norm_path);
		// kfree(parent);
		// kfree(name);
		// return res;
	}
	dentry_add(child); // Now track the new dentry in the hashtable

	*out_dentry = child;

free_all:
	dput(pdentry);
	kfree(norm_path);
	kfree(parent);
	kfree(name);
	return res;
}

/**
 * @brief Creates a new directory.
 *
 * @param path Path to the directory to create. Resolved against the
 * current task's working directory if relative.
 * @param mode Permission bits for the new directory.
 *
 * @return 0 on success, or a negative errno on error.
 */
int vfs_mkdir(const char* path, uint16_t mode)
{
	if (!path) {
		return -EINVAL;
	}

	if (strcmp(path, "/") == 0) {
		return -EEXIST;
	}

	char* norm_path = vfs_normalize_path(path, get_current_task()->cwd);
	if (!norm_path) {
		return -ENOMEM;
	}

	char* parent;
	char* name;
	int res = __split_path(norm_path, &parent, &name);

	if (res < 0 || !parent || !name) {
		kfree(norm_path);
		if (parent) kfree(parent);
		if (name) kfree(name);
		return res;
	}

	struct vfs_dentry* pdentry = vfs_lookup(parent);
	if (!pdentry) {
		kfree(norm_path);
		kfree(parent);
		kfree(name);
		return -ENOENT;
	}

	struct vfs_inode* pinode = pdentry->inode;

	// Optionally, check for existing child with the same name
	if (vfs_does_name_exist(pdentry, name)) {
		dput(pdentry);
		kfree(norm_path);
		kfree(parent);
		kfree(name);
		return -EEXIST;
	}

	struct vfs_dentry* child = dentry_alloc(pdentry, name);
	if (!child) {
		dput(pdentry);
		kfree(norm_path);
		kfree(parent);
		kfree(name);
		return -ENOMEM;
	}

	if (!pinode->ops || !pinode->ops->mkdir) {
		dput(pdentry);
		kfree(norm_path);
		kfree(parent);
		kfree(name);
		return -ENODEV;
	}

	res = pinode->ops->mkdir(pinode, child, mode);
	if (res < 0) {
		dput(pdentry);
		dentry_dealloc(child);
		kfree(norm_path);
		kfree(parent);
		kfree(name);
		return res;
	}

	dentry_add(child);
	dput(pdentry);

	kfree(norm_path);
	kfree(parent);
	kfree(name);
	return 0;
}

/**
 * @brief Writes to a file at a given offset, via its file_ops.
 *
 * @param file File to write to.
 * @param buffer Bytes to write.
 * @param count Number of bytes to write.
 * @param offset Offset to write at.
 *
 * @return Number of bytes written, 0 if count is 0, or a negative
 * errno on error.
 */
ssize_t __vfs_pwrite(struct vfs_file* file, const char* buffer, size_t count, off_t* offset)
{
	if (!file || !offset || !buffer) {
		return -EINVAL;
	}

	if (count == 0) {
		return 0;
	}

	if (!file->fops || !file->fops->write) {
		return -ENOSYS;
	}

	return file->fops->write(file, buffer, count, offset);
}

/**
 * @brief Writes to a file at its current position, advancing it.
 *
 * @param file File to write to.
 * @param buffer Bytes to write.
 * @param count Number of bytes to write.
 *
 * @return Number of bytes written, or a negative errno on error.
 */
ssize_t vfs_file_write(struct vfs_file* file, const char* buffer, size_t count)
{
	if (!file || !buffer) {
		return -EINVAL;
	}

	if (count == 0) {
		return 0;
	}

	return __vfs_pwrite(file, buffer, count, &file->f_pos);
}

/**
 * @brief Writes to a file descriptor at its current position.
 *
 * @param fd File descriptor to write to.
 * @param buffer Bytes to write.
 * @param count Number of bytes to write.
 *
 * @return Number of bytes written, or a negative errno on error.
 */
ssize_t vfs_write(int fd, const char* buffer, size_t count)
{
	// TODO: Handle O_APPEND
	if (!buffer) {
		return -EINVAL;
	}

	if (count == 0) {
		return 0;
	}

	struct vfs_file* file = get_file(fd);

	if (!file) {
		return -EBADF;
	}

	return vfs_file_write(file, buffer, count);
}

/**
 * @brief Writes to a file descriptor at a given offset.
 *
 * @param fd File descriptor to write to.
 * @param buffer Bytes to write.
 * @param count Number of bytes to write.
 * @param offset Offset to write at. Does not change the file's current
 * position.
 *
 * @return Number of bytes written, or a negative errno on error.
 */
ssize_t vfs_pwrite(int fd, const char* buffer, size_t count, off_t offset)
{
	if (!buffer) {
		return -EINVAL;
	}

	if (count == 0) {
		return 0;
	}

	struct vfs_file* file = get_file(fd);

	if (!file) {
		return -EBADF;
	}

	return __vfs_pwrite(file, buffer, count, &offset);
}

/**
 * @brief Reads from a file at a given offset, via its file_ops.
 *
 * @param file File to read from.
 * @param buffer Buffer to read into.
 * @param count Maximum number of bytes to read.
 * @param offset Offset to read from.
 *
 * @return Number of bytes read, 0 if count is 0, or a negative errno
 * on error.
 */
ssize_t __vfs_pread(struct vfs_file* file, char* buffer, size_t count, off_t* offset)
{
	if (!file || !offset || !buffer) {
		return -EINVAL;
	}

	if (count == 0) {
		return 0;
	}

	if (!file->fops || !file->fops->read) {
		return -ENOSYS;
	}

	return file->fops->read(file, buffer, count, offset);
}

/**
 * @brief Reads from a file at its current position, advancing it.
 *
 * @param file File to read from.
 * @param buffer Buffer to read into.
 * @param count Maximum number of bytes to read.
 *
 * @return Number of bytes read, or a negative errno on error.
 */
ssize_t vfs_file_read(struct vfs_file* file, char* buffer, size_t count)
{
	if (!file || !buffer) {
		return -EINVAL;
	}

	if (count == 0) {
		return 0;
	}

	return __vfs_pread(file, buffer, count, &file->f_pos);
}

/**
 * @brief Reads from a file descriptor at its current position.
 *
 * @param fd File descriptor to read from.
 * @param buffer Buffer to read into.
 * @param count Maximum number of bytes to read.
 *
 * @return Number of bytes read, or a negative errno on error.
 */
ssize_t vfs_read(int fd, char* buffer, size_t count)
{
	if (!buffer) {
		return -EINVAL;
	}

	if (count == 0) {
		return 0;
	}

	struct vfs_file* file = get_file(fd);

	if (!file) {
		return -EBADF;
	}

	return vfs_file_read(file, buffer, count);
}

/**
 * @brief Reads from a file descriptor at a given offset.
 *
 * @param fd File descriptor to read from.
 * @param buffer Buffer to read into.
 * @param count Maximum number of bytes to read.
 * @param offset Offset to read from. Does not change the file's
 * current position.
 *
 * @return Number of bytes read, or a negative errno on error.
 */
ssize_t vfs_pread(int fd, char* buffer, size_t count, off_t offset)
{
	if (!buffer) {
		return -EINVAL;
	}

	if (count == 0) {
		return 0;
	}

	struct vfs_file* file = get_file(fd);

	if (!file) {
		return -EBADF;
	}

	return __vfs_pread(file, buffer, count, &offset);
}

/**
 * @brief Repositions a file descriptor's read/write offset.
 *
 * @param fd File descriptor to seek.
 * @param offset Offset, relative to whence.
 * @param whence SEEK_SET, SEEK_CUR, or SEEK_END.
 *
 * @return The new offset on success, or a negative errno on error.
 */
off_t vfs_lseek(int fd, off_t offset, int whence)
{
	struct vfs_file* file = get_file(fd);
	if (!file) {
		return -EBADF;
	}

	switch (whence) {
	case SEEK_SET: file->f_pos = offset; return file->f_pos;
	case SEEK_CUR:
		if (file->f_pos + offset < 0) break;
		file->f_pos += offset;
		return file->f_pos;
	case SEEK_END: file->f_pos = (off_t)file->dentry->inode->f_size + offset; return file->f_pos;
	default:       break;
	}

	return -EINVAL;
}

/**
 * @brief Gets the open file for a file descriptor of the current task.
 *
 * @param fd File descriptor to look up.
 *
 * @return The open file, or nullptr if fd is out of range or not open.
 */
struct vfs_file* get_file(int fd)
{
	if (fd >= MAX_RESOURCES || fd < 0) {
		return nullptr;
	}

	struct task* current_task = get_current_task();

	return current_task->resources[fd];
}

/**
 * @brief Checks whether a directory already has a child with the
 * given name.
 *
 * @param parent Directory dentry to search.
 * @param name Name to look for.
 *
 * @return true if a child named name exists, false otherwise.
 *
 * @note Only checks the in-memory children list. Does not query the
 * filesystem.
 */
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
 * @brief Finds a registered filesystem type by name.
 *
 * @param fs_type Filesystem name, e.g. "ramfs".
 *
 * @return The matching vfs_fs_type, or nullptr if none is registered.
 */
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
 * @brief Mounts a filesystem at a given path.
 *
 * @param source Device to mount, for example "/dev/sda1". May be nullptr
 * for ramfs or other virtual devices.
 * @param target Path to mount at.
 * @param fstype Filesystem type to mount.
 * @param flags Mount flags.
 */
int vfs_mount(const char* source, const char* target, const char* fstype, int flags)
{
	// Find the filesystem type (e.g., "fat32", "ramfs") in registered filesystems.
	struct vfs_fs_type* fs = find_filesystem(fstype);
	if (!fs) {
		return -ENODEV; // Filesystem type not found
	}

	// Find the directory in the VFS that we want to mount on.
	struct vfs_dentry* mount_point_dentry = vfs_lookup(target);
	if (!mount_point_dentry) {
		return -ENOENT; // Mount point doesn't exist
	}
	// TODO: Add a check to ensure mount_point_dentry is a directory.

	// Call the filesystem-specific mount function.
	struct vfs_superblock* sb = fs->mount(source, flags);
	if (!sb) {
		dput(mount_point_dentry);
		return -ENODEV; // The FS failed to mount
	}

	// The dentry for the mount point should now point to the new superblock's root.
	struct vfs_inode* old = mount_point_dentry->inode;
	mount_point_dentry->inode = sb->root_dentry->inode;

	if (mount_point_dentry->inode) {
		iget(mount_point_dentry->inode);
	}

	if (old) {
		iput(old);
		old->ref_count--;
	}

	struct vfs_mount* new_mount = (struct vfs_mount*)kmalloc(sizeof(struct vfs_mount));
	new_mount->mount_point = strdup(target);
	sb->mount_point = new_mount->mount_point;
	new_mount->sb = sb;
	new_mount->flags = flags;
	register_mount(new_mount);

	dput(mount_point_dentry);
	log_info("Mounted %s on %s type %s", source, target, fstype);
	return 0;
}

/**
 * @brief Resolves a path to a dentry.
 *
 * @param path Absolute or relative path. Relative paths are resolved
 * against the current task's working directory.
 *
 * @return A referenced dentry on success. The caller must call
 * dput(). nullptr if the path does not exist.
 *
 * @note Panics if called before the root filesystem is mounted.
 */
struct vfs_dentry* vfs_lookup(const char* path)
{
	// If the VFS isn't even mounted, it's a fatal error.
	if (g_vfs_root_mount == nullptr) {
		panic("VFS lookup called before rootfs was mounted!");
	}

	// This can be called with no cwd early in boot
	struct task* t = get_current_task();
	struct vfs_dentry* base = t ? t->cwd : g_vfs_root_mount->sb->root_dentry;
	char* norm_path = vfs_normalize_path(path, base);

	struct vfs_dentry* current_dentry = __vfs_walk_path(g_vfs_root_mount->sb->root_dentry, norm_path);

	kfree(norm_path);
	return current_dentry;
}

/**
 * @brief Gets the superblock of the filesystem that contains a path.
 *
 * @param path Path to look up.
 *
 * @return The superblock, or nullptr if path does not exist.
 */
struct vfs_superblock* vfs_get_sb(const char* path)
{
	struct vfs_dentry* dentry = vfs_lookup(path);
	if (!dentry || !dentry->inode || !dentry->inode->sb) {
		dput(dentry);
		return nullptr;
	}
	struct vfs_superblock* sb = dentry->inode->sb;
	dput(dentry);
	return sb;
}

int uuid = 1; // Always points to next available id, 0 = invalid id

/**
 * @brief Allocates a new unique ID.
 *
 * @return A unique ID, never 0.
 */
int vfs_get_next_id()
{
	return uuid++;
}

/**
 * @brief Gets the most recently allocated unique ID.
 *
 * @return The ID last returned by vfs_get_next_id().
 */
int vfs_get_id()
{
	return uuid - 1;
}

// TODO: Make this use path_component struct
/**
 * @brief Gets the next slash-delimited component of a path.
 *
 * @param tok Tokenizer state. Advances on each call.
 * @param out_len Receives the length of the returned component.
 *
 * @return Pointer to the start of the next component within tok's
 * path, or nullptr at the end of the path. Not NUL-terminated.
 */
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
	while (tok->path[tok->offset] != '/' && tok->path[tok->offset] != '\0') {
		tok->offset++;
	}

	*out_len = tok->offset - start_offset;

	return token_start;
}

/**
 * @brief Resolves a relative path starting from a root dentry.
 *
 * @param root Starting directory dentry. Must be a directory.
 * @param path Relative path to resolve, for example "foo/bar.txt".
 *
 * @return Pointer to the final vfs_dentry on success, or NULL on failure.
 */
struct vfs_dentry* __vfs_walk_path(struct vfs_dentry* root, const char* path)
{
	log_debug("Walking path '%s' from root '%s'", path, root->name);
	const char* token;
	size_t len = 0;
	struct path_tokenizer tok = { .path = path };
	struct vfs_dentry* parent = dget(root);

	while ((token = path_next_token(&tok, &len))) {
		log_debug("Walking token: '%.*s'", (int)len, token);
		char token_buf[len + 1];
		memcpy(token_buf, token, len);
		token_buf[len] = '\0';

		struct vfs_dentry* child = __dentry_lookup(parent, token_buf);
		dput(parent);
		if (!child) {
			return nullptr;
		}
		parent = child;
	}

	return parent;
}

/**
 * @brief Allocates and initializes a new dentry, with no inode.
 *
 * @param parent Parent directory dentry.
 * @param name Name to give the new dentry. Copied.
 *
 * @return A new dentry with a reference count of 1, or nullptr on
 * allocation failure.
 *
 * @note Does not add the dentry to the hash table or its parent's
 * children list; the caller does that once the inode is set up. Do
 * not call dget() on the result: it already has a reference of one.
 */
struct vfs_dentry* dentry_alloc(struct vfs_dentry* parent, const char* name)
{
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

	INIT_HLIST_NODE(&dentry->hash);

	return dentry;
}

/**
 * @brief Frees a dentry allocated by dentry_alloc().
 *
 * @param d Dentry to free.
 *
 * @note Refuses to free a dentry that still has children. Called by
 * dput() when a dentry's reference count reaches zero; do not call
 * directly on a hashed or referenced dentry.
 */
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

	slab_free(&dentry_cache, d);
}

/**
 * @brief Builds the absolute path of a dentry by walking up to the
 * root.
 *
 * @param dentry Dentry to build the path for.
 *
 * @return A newly allocated absolute path string, or nullptr on
 * allocation failure or if the path is too deep (more than 256
 * components).
 */
char* dentry_to_abspath(struct vfs_dentry* dentry)
{
	// Handle root case early
	if (!dentry || !dentry->parent) {
		return strdup("/");
	}

	// Walk up to root
	struct path_component stack[256];
	int stack_depth = 0;

	// Stop before root
	while (dentry && dentry->parent) {
		if (stack_depth >= 256) {
			return nullptr; // Path too deep
		}
		stack[stack_depth].start = dentry->name;
		stack[stack_depth].len = strlen(dentry->name);
		stack_depth++;
		dentry = dentry->parent;
	}

	if (stack_depth == 0) {
		return strdup("/"); // Root case
	}

	size_t result_len = 1;

	for (int i = 0; i < stack_depth; i++) {
		log_debug("Component %d: '%.*s'", i, (int)stack[i].len, stack[i].start);
		result_len += stack[i].len;
		if (i < stack_depth - 1) {
			result_len += 1; // Separator '/'
		}
	}

	char* result = kmalloc(result_len + 1);
	if (!result) {
		return nullptr;
	}

	size_t pos = 0;
	result[pos++] = '/'; // Start with '/'
	for (int i = stack_depth - 1; i >= 0; i--) {
		memcpy(result + pos, stack[i].start, stack[i].len);
		pos += stack[i].len;

		// Add separator except after last component
		if (i > 0) {
			result[pos++] = '/';
		}
	}

	result[pos] = '\0';

	return result;
}

/**
 * @brief Resolves a path against a base directory into a canonical
 * absolute path.
 *
 * @param path Absolute or relative path. May contain "." and ".."
 * components.
 * @param base_dir Directory to resolve a relative path against.
 * Ignored if path is absolute.
 *
 * @return A newly allocated, canonical absolute path string, or
 * nullptr on error (invalid base_dir, empty or too-long path,
 * allocation failure, or a path more than 256 components deep).
 */
char* vfs_normalize_path(const char* path, struct vfs_dentry* base_dir)
{
	if (!base_dir || !base_dir->inode || base_dir->inode->filetype != FILETYPE_DIR) {
		panic("TATDEHJS");
		log_error("Inavlid base dir");
		return nullptr;
	}

	size_t path_len = strlen(path);
	if (path_len == 0 || path_len >= VFS_MAX_PATH) {
		return nullptr;
	}

	bool is_absolute = (path[0] == '/');
	char* abs_path;
	if (is_absolute) {
		abs_path = strdup("/"); // Start from root
	} else {
		abs_path = dentry_to_abspath(base_dir);
	}
	if (!abs_path) {
		return nullptr;
	}

	struct path_component stack[256];
	int stack_depth = 0;

	const char* token;
	size_t len = 0;
	struct path_tokenizer tok = { .path = abs_path };

	while ((token = path_next_token(&tok, &len))) {
		// Not going to handle "." or ".." since that should never be in the abs_path
		// If it is in there I will thrash whoever made dentry_to_absolute_path
		if (stack_depth >= 256) {
			kfree(abs_path);
			return nullptr; // Path too deep
		}
		stack[stack_depth].start = token;
		stack[stack_depth].len = len;
		stack_depth++;
	}

	tok = (struct path_tokenizer) { .path = path };

	while ((token = path_next_token(&tok, &len))) {
		if (len == 1 && strncmp(token, ".", 1) == 0) {
			// Current directory - skip
			continue;
		} else if (len == 2 && strncmp(token, "..", 2) == 0) {
			// Parent directory - pop from stack
			if (stack_depth > 0) {
				stack_depth--;
			}
		} else {
			// Regular component - push to stack
			if (stack_depth >= 256) {
				kfree(abs_path);
				return nullptr; // Path too deep
			}
			stack[stack_depth].start = token;
			stack[stack_depth].len = len;
			stack_depth++;
		}
	}

	if (stack_depth == 0) {
		char* result = kmalloc(2);
		if (result) {
			result[0] = '/';
			result[1] = '\0';
		}
		kfree(abs_path);
		return result;
	}

	size_t result_len = 1; // Leading '/'
	for (int i = 0; i < stack_depth; i++) {
		log_debug("Component %d: '%.*s'", i, (int)stack[i].len, stack[i].start);
		result_len += stack[i].len;
		if (i < stack_depth - 1) {
			result_len += 1; // '/' separator
		}
	}

	char* result = (char*)kmalloc(result_len + 1); // +1 for null terminator
	if (!result) {
		kfree(abs_path);
		return nullptr;
	}

	size_t pos = 0;
	result[pos++] = '/';

	for (int i = 0; i < stack_depth; i++) {
		memcpy(result + pos, stack[i].start, stack[i].len);
		pos += stack[i].len;

		// Avoid trailing '/'
		if (i < stack_depth - 1) {
			result[pos++] = '/';
		}
	}

	result[pos] = '\0';

	kfree(abs_path);

	log_debug("Normalized path: %s", result);

	return result;
}

#if defined(HELIOS_TESTS)

#include "kernel/ktest.h"

KTEST(test_path_tokenizer)
{
	static const struct {
		const char* path;
		const char* expected[8];
		int count;
	} cases[] = {
		{ "/foo/bar/baz/qux", { "foo", "bar", "baz", "qux" }, 4 },
		{ "foo/bar", { "foo", "bar" }, 2 },
		{ "//foo//bar//", { "foo", "bar" }, 2 },
		{ "/", { }, 0 },
		{ "", { }, 0 },
	};

	int fails = 0;

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		struct path_tokenizer tok = { .path = cases[i].path };
		size_t len = 0;
		int n = 0;
		const char* token;

		while ((token = path_next_token(&tok, &len))) {
			if (n >= cases[i].count) {
				log_error("[T%zu] extra token '%.*s'", i, (int)len, token);
				fails++;
				break;
			}
			if (strlen(cases[i].expected[n]) != len || strncmp(token, cases[i].expected[n], len) != 0) {
				log_error("[T%zu] token[%d]: got '%.*s', want '%s'",
					  i,
					  n,
					  (int)len,
					  token,
					  cases[i].expected[n]);
				fails++;
			}
			n++;
		}

		if (n < cases[i].count) {
			log_error("[T%zu] got %d tokens, want %d for path='%s'", i, n, cases[i].count, cases[i].path);
			fails++;
		}
	}

	return fails;
}

/* Self-test for parse_path_components().
 * Returns number of failed checks; 0 means all tests passed.
 * Assumes:
 *   - VFS_OK == 0
 *   - Negative error codes like -VFS_ERR_INVAL, -VFS_ERR_NAMETOOLONG
 *   - VFS_MAX_NAME defined (e.g., 255)
 *   - log_info/log_error available
 *   - kfree available (to free outputs on success cases)
 *
 *   I'll be honest this shit is ChatGPT
 */
KTEST(test_split_path)
{
	size_t fails = 0;
	size_t tests = 0;

	struct test_case {
		const char* path;
		int exp_rc;
		const char* exp_parent; /* nullptr means expect error */
		const char* exp_name;	/* nullptr means expect error */
	};

	/* Core success and error cases. */
	static const struct test_case cases[] = {
		/* --- Success cases --- */
		{ "/a/b/c", 0, "/", "c" }, /* parent will be "/a/b" (verified below) */
		{ "/a/b//c///", 0, "/a/b", "c" },
		{ "a/b/c", 0, "a/b", "c" },
		{ "a////b", 0, "a", "b" },
		{ "/c", 0, "/", "c" },
		{ "c", 0, ".", "c" },
		{ "./a", 0, ".", "a" },
		{ "//a", 0, "/", "a" },
		{ "a/../b", 0, "a/..", "b" },
		{ "/.hidden", 0, "/", ".hidden" },

		/* --- Error cases --- */
		{ "", -EINVAL, nullptr, nullptr },
		{ "/", -EINVAL, nullptr, nullptr },
		{ "////", -EINVAL, nullptr, nullptr },
		{ "a/.", -EINVAL, nullptr, nullptr },
		{ "a/..", -EINVAL, nullptr, nullptr },

		/* Additional edge-y successes */
		{ "a//", 0, ".", "a" },
		{ "///a///", 0, "/", "a" },
	};

	/* Run the table-driven tests. */
	for (size_t t = 0; t < sizeof(cases) / sizeof(cases[0]); ++t) {
		const struct test_case* tc = &cases[t];
		char* parent = (char*)0x1; /* sentinel non-nullptr so we can verify error paths null them */
		char* name = (char*)0x1;

		int rc = __split_path(tc->path, &parent, &name);
		++tests;

		if (tc->exp_rc == 0) {
			if (rc != 0) {
				log_error("[T%zu] expected 0, got %d for path='%s'", t, rc, tc->path);
				++fails;
			}
			if (!parent || !name) {
				log_error("[T%zu] outputs are nullptr on success for path='%s'", t, tc->path);
				++fails;
			} else {
				/* Parent can be more than just "/" or "."; check exact string expectations. */
				if (strcmp(tc->exp_parent, "/") == 0 && strcmp(tc->path, "/a/b/c") == 0) {
					/* Special verify for the first test: parent should be "/a/b". */
					if (strcmp(parent, "/a/b") != 0) {
						log_error("[T%zu] parent mismatch path='%s' got='%s' want='/a/b'",
							  t,
							  tc->path,
							  parent);
						++fails;
					}
				} else {
					if (strcmp(parent, tc->exp_parent) != 0) {
						log_error("[T%zu] parent mismatch path='%s' got='%s' want='%s'",
							  t,
							  tc->path,
							  parent,
							  tc->exp_parent);
						++fails;
					}
				}
				if (strcmp(name, tc->exp_name) != 0) {
					log_error("[T%zu] name mismatch path='%s' got='%s' want='%s'",
						  t,
						  tc->path,
						  name,
						  tc->exp_name);
					++fails;
				}
			}
			/* Always free on success to avoid leaks even if a check failed. */
			if (parent) kfree(parent);
			if (name) kfree(name);
		} else {
			/* Expect an error. */
			if (rc != tc->exp_rc) {
				log_error("[T%zu] expected rc=%d, got %d for path='%s'", t, tc->exp_rc, rc, tc->path);
				++fails;
			}
			if (parent != nullptr || name != nullptr) {
				log_error("[T%zu] outputs must be nullptr on error for path='%s' (parent=%p, name=%p)",
					  t,
					  tc->path,
					  (void*)parent,
					  (void*)name);
				++fails;
				/* Defensive: avoid freeing sentinels. */
				if (parent && parent != (char*)0x1) kfree(parent);
				if (name && name != (char*)0x1) kfree(name);
			}
		}
	}

	/* ---- Length boundary tests for VFS_MAX_NAME ---- */

	/* Too-long name: "x/" + (VFS_MAX_NAME+1) of 'a' -> -VFS_ERR_NAMETOOLONG */
	{
		const size_t too_long = VFS_MAX_NAME + 1;
		char buf[VFS_MAX_NAME + 4 + 8]; /* "x/" + name + NUL; a little slack */
		char* p = buf;

		*p++ = 'x';
		*p++ = '/';
		for (size_t i = 0; i < too_long; ++i)
			*p++ = 'a';
		*p = '\0';

		char* parent = (char*)0x1;
		char* name = (char*)0x1;
		int rc = __split_path(buf, &parent, &name);
		++tests;
		if (rc != -ENAMETOOLONG) {
			log_error("[LEN1] expected -VFS_ERR_NAMETOOLONG, got %d for path of len=%zu", rc, strlen(buf));
			++fails;
		}
		if (parent != nullptr || name != nullptr) {
			log_error("[LEN1] outputs must be nullptr on error (parent=%p, name=%p)",
				  (void*)parent,
				  (void*)name);
			++fails;
			if (parent && parent != (char*)0x1) kfree(parent);
			if (name && name != (char*)0x1) kfree(name);
		}
	}

	/* Exactly-at-limit name: "x/" + (VFS_MAX_NAME) of 'a' -> success, name length == VFS_MAX_NAME */
	{
		const size_t exact = VFS_MAX_NAME;
		char buf[VFS_MAX_NAME + 4 + 8];
		char* p = buf;

		*p++ = 'x';
		*p++ = '/';
		for (size_t i = 0; i < exact; ++i)
			*p++ = 'a';
		*p = '\0';

		char* parent = nullptr;
		char* name = nullptr;
		int rc = __split_path(buf, &parent, &name);
		++tests;

		if (rc != 0) {
			log_error("[LEN2] expected 0, got %d", rc);
			++fails;
		} else {
			if (!parent || !name) {
				log_error("[LEN2] outputs are nullptr on success");
				++fails;
			} else {
				if (strcmp(parent, "x") != 0) {
					log_error("[LEN2] parent mismatch got='%s' want='x'", parent);
					++fails;
				}
				size_t nlen = strlen(name);
				if (nlen != VFS_MAX_NAME) {
					log_error("[LEN2] name length mismatch got=%zu want=%zu",
						  nlen,
						  (size_t)VFS_MAX_NAME);
					++fails;
				}
			}
		}
		if (parent) kfree(parent);
		if (name) kfree(name);
	}

	log_info("parse_path_components: %zu/%zu tests passed", tests - fails, tests);

	return (int)fails;
}
#endif

/** @} */
