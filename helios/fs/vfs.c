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

#include <uapi/helios/errno.h>

#undef LOG_LEVEL
#define LOG_LEVEL 1
#define FORCE_LOG_REDEF
#include <lib/log.h>
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

struct path_component {
	const char* start; // Points into original string
	size_t len;
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

/**
 * @brief Splits a filesystem path into parent directory and basename.
 *
 * @param path       Input path string (must be non-NULL).
 * @param parent_out On success, allocated buffer containing parent path.
 * @param name_out   On success, allocated buffer containing basename.
 *
 * @return VFS_OK on success, negative VFS_ERR_* on error.
 *
 * @note Both @p parent_out and @p name_out must be freed with kfree() by the caller.
 */
static int __split_path(const char* path, char** parent_out, char** name_out);

/*******************************************************************************
 * Public Function Definitions
 *******************************************************************************/

/**
 * vfs_init - Initializes the virtual filesystem.
 */
void vfs_init()
{
	sb_list = (struct vfs_superblock**)kmalloc(sizeof(*sb_list) * 8);

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

	sem_init(&inode->lock, 1);

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
 * dget - Acquire a counted reference to a dentry
 * @dentry: Dentry to reference (may be NULL)
 *
 * Increments @dentry->ref_count and returns @dentry. Use dget() whenever:
 *  - You will return an existing (already-cached) dentry to a caller
 *    (e.g., a hash-table hit in dentry_lookup()).
 *  - You store a dentry into a structure that outlives the current scope
 *    (e.g., file->dentry), transferring ownership to that structure.
 *
 * Do not add an extra reference when returning the freshly-allocated `child`
 * that was passed into a filesystem ->lookup() implementation; that dentry
 * already has refcount==1 from dentry_alloc().
 *
 * Return: @dentry (unchanged), or NULL if @dentry was NULL.
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
 * dput - Release a counted reference to a dentry
 * @dentry: Dentry to release (may be NULL)
 *
 * Decrements @dentry->ref_count. When the count reaches zero, the dentry is
 * torn down: iput() is called on its inode and the dentry memory is freed.
 *
 * Typical balanced pairs:
 *  - vfs_walk_path(): dget(root) on entry; dput(prev) each hop.
 *  - vfs_open(): on any failure after a successful lookup, dput(dentry).
 *  - vfs_close(): when a file’s last ref is dropped, dput(file->dentry).
 *  - vfs_mount(): after grafting, dput(mount_point_dentry).
 *  - vfs_create()/vfs_mkdir(): always dput(parent) before returning.
 *
 * Notes:
 *  - This helper is NULL-safe; passing NULL is a no-op.
 *  - After dput(), the caller must not dereference @dentry unless it still
 *    holds another reference elsewhere.
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
 * dentry_add - Adds a dentry to the hash table.
 * @dentry: Pointer to the vfs_dentry structure to be added.
 */
void dentry_add(struct vfs_dentry* dentry)
{
	u32 hash = dentry_hash(dentry);
	struct hlist_head* bucket = &d_ht[hash_min(hash, HASH_BITS(d_ht))];
	dentry->bucket = bucket;
	hash_add(d_ht, &dentry->hash, hash);
	dget(dentry);
	log_debug("Added dentry %s to hash table, ref_count: %d",
		  dentry->name,
		  dentry->ref_count);
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
 * dentry_lookup - Find or construct a child dentry under @parent
 * @parent: Directory dentry to search
 * @name:   Child name
 *
 * Semantics & ownership:
 *  - On a CACHE HIT: return dget(found); the caller owns one reference.
 *  - On a MISS: call parent->inode->ops->lookup(parent->inode, child),
 *    where @child is the freshly-allocated dentry from dentry_alloc().
 *    The filesystem must:
 *      * Populate @child (and insert with dentry_add(child) if it exists), and
 *        then return @child WITHOUT adding another reference; OR
 *      * If it decides to return a DIFFERENT existing dentry, it must
 *        dget(existing) and arrange to drop/dealloc the unused @child.
 *
 * Return: Referenced dentry on success (caller must dput()), or NULL on error.
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

	if (!parent->inode || !parent->inode->ops ||
	    !parent->inode->ops->lookup) {
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

/**
 * __fill_dirent - Populate a VFS dirent from a (stable) dentry
 * @dentry: Dentry whose inode/name/type will be copied (must not be freed during the call)
 * @dirent:        Output record to fill (caller-provided)
 *
 * Copies the inode number, type, record length policy, and name from @locked_dentry
 * into @dirent. This helper does not set @dirent->d_off; the caller (typically
 * the VFS readdir wrapper) is responsible for assigning the resume position.
 *
 * Type mapping:
 *    Maps internal FILETYPE_* to DT_* (DT_UNKNOWN when the type cannot be determined).
 *
 * Name handling:
 *    Copies up to NAME_MAX bytes and NUL-terminates. If the underlying name exceeds
 *    NAME_MAX, the current behavior truncates; consider enforcing a strict policy
 *    (reject/skip with a warning) to avoid silent truncation.
 *
 * Record length:
 *    Sets d_reclen according to the in-kernel policy. My policy is currently a fixed-size
 *    record, this will be sizeof(struct dirent). If I later adopt variable-length
 *    records, compute header + strlen(name)+1 and align as needed.
 *
 * Return:
 *    VFS_OK (0) on success; negative -VFS_ERR_* if I add stricter validation and
 *    choose to signal oversize names or invalid inputs.
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
	dirent->d_reclen = sizeof(struct dirent);

	strncpy(dirent->d_name, dentry->name, 255);
	dirent->d_name[255] = '\0';

	return VFS_OK;
}

/**
 * vfs_readdir - Iterate a directory one entry at a time (VFS view)
 * @dir:    Opened directory file object (must reference a directory inode)
 * @out:    Output dirent to fill
 * @offset: Global position (a.k.a. "cookie"): 0=".", 1="..", >=2=children
 *
 * Semantics:
 *    - Positions 0 and 1 are synthesized by the VFS for "." and ".." respectively.
 *      For these, @out->d_off is set to the next global position (1 for ".",
 *      2 for "..") and @dir->f_pos is updated to match.
 *
 *    - For positions >= 2, the VFS translates the global position to a filesystem
 *      child index as: child_index = @offset - 2, and invokes the filesystem's
 *      ->readdir() with that child_index. The filesystem returns one entry and
 *      sets @out->d_off to the next child index (typically child_index+1 for
 *      simple list-ordered directories). The VFS then converts this back to a global
 *      position by adding 2:
 *
 *           out->d_off  = (filesystem_next_child_index) + 2;
 *           dir->f_pos  = out->d_off;   // stateful "next" position
 *
 *    - This function always returns at most one entry per call.
 *
 * Returns:
 *    @retval  1  One entry was emitted and @out is valid; @out->d_off is where to resume.
 *    @retval  0  End of directory (no entry at @offset / no more children).
 *    @retval <0  Negative -VFS_ERR_* on error (e.g., -VFS_ERR_INVAL, -VFS_ERR_NOTDIR,
 *                -VFS_ERR_OPNOTSUPP).
 *
 * Locking & concurrency:
 *    - Acquires the directory inode's read lock for the duration of the operation.
 *      Filesystem mutations (create/unlink/rename) should take the write lock.
 *    - Provides a best-effort snapshot: under concurrent mutations, an iterator may
 *      skip or re-see entries but must never crash or return partially initialized data.
 *
 * Interaction with lseek/f_pos:
 *    - @offset is a global position compatible with directory lseek: 0=".", 1="..",
 *      >=2 children. Implementations should treat absurdly large @offset as EOF (0),
 *      not an error.
 *    - On a successful emission, @dir->f_pos is advanced to @out->d_off so a subsequent
 *      "read next" can use @dir->f_pos as its starting point.
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

ssize_t vfs_getdents(int fd, struct dirent* dirp, size_t count)
{
	size_t num_dirp = count / sizeof(struct dirent);
	log_info("vfs_getdents: fd=%d, dirp=%p, count=%zu (num_dirp=%zu)",
		 fd,
		 (void*)dirp,
		 count,
		 num_dirp);
	for (size_t i = 0; i < num_dirp; i++) {
		int res = vfs_readdir(get_file(fd), &dirp[i], DIRENT_GET_NEXT);
		if (res < 0) {
			return (ssize_t)res;
		} else if (res == 0) {
			// End of directory
			return (ssize_t)(i * sizeof(struct dirent));
		}
	}

	return (ssize_t)(num_dirp * sizeof(struct dirent));
}

int __vfs_open_for_task(struct task* t, const char* path, int flags)
{
	char* norm_path = vfs_normalize_path(path, t->cwd);
	struct vfs_dentry* dentry = vfs_lookup(norm_path);
	if (!dentry || !dentry->inode) {
		log_debug("Dentry not found for path: %s", path);
		if (flags & O_CREAT) {
			int res = vfs_create(norm_path,
					     VFS_PERM_ALL,
					     flags,
					     &dentry);
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
	log_debug("Opened file %s with fd %d and dref_count %d",
		  dentry->name,
		  fd,
		  dentry->ref_count);

	kfree(norm_path);
	return fd;
}

int vfs_open(const char* path, int flags)
{
	return __vfs_open_for_task(get_current_task(), path, flags);
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

int vfs_access(const char* path, int amode)
{
	// Just going to skip amode for now :)
	(void)amode;

	log_info("path=%s, amode=%d", path, amode);
	struct vfs_dentry* dentry = vfs_lookup(path);
	if (!dentry || !dentry->inode) {
		return -VFS_ERR_NOENT;
	}

	// TODO: Check permissions

	dput(dentry);
	return 0;
}

void register_child(struct vfs_dentry* parent, struct vfs_dentry* child)
{
	if (!parent || !child) {
		return;
	}

	list_add_tail(&parent->children, &child->siblings);
}

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

int vfs_create(const char* path,
	       uint16_t mode,
	       int flags,
	       struct vfs_dentry** out_dentry)
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
	if (!pdentry || !pdentry->inode ||
	    !(pdentry->inode->filetype == FILETYPE_DIR)) {
		dput(pdentry);
		kfree(norm_path);
		kfree(parent);
		kfree(name);
		return -ENOTDIR;
	}

	// Try to lookup the file by name
	struct vfs_dentry* child = __dentry_lookup(pdentry, name);

	if (child && child->inode) {
		log_debug("child: %p, name: %p", child->name, name);
		if (flags & O_EXCL) {
			dput(child);
			kfree(norm_path);
			kfree(parent);
			kfree(name);
			return -EEXIST;
		}
		// File exists but not O_EXCL — treat as success?
		*out_dentry = child;
		dput(pdentry);
		kfree(norm_path);
		kfree(parent);
		kfree(name);
		return 0;
	}

	child = dentry_alloc(pdentry, name);
	log_debug("child: %p, name: %p", child->name, name);
	if (!child) {
		dput(pdentry);
		kfree(norm_path);
		kfree(parent);
		kfree(name);
		return -ENOMEM;
	}

	if (!pdentry->inode->ops || !pdentry->inode->ops->create) {
		dentry_dealloc(child);
		dput(pdentry);
		kfree(norm_path);
		kfree(parent);
		kfree(name);
		return -ENODEV;
	}

	res = pdentry->inode->ops->create(pdentry->inode, child, mode);
	if (res < 0) {
		dentry_dealloc(child);
		dput(pdentry);
		kfree(norm_path);
		kfree(parent);
		kfree(name);
		return res;
	}
	dentry_add(child); // Now track the new dentry in the hashtable
	dput(pdentry);

	*out_dentry = child;

	kfree(norm_path);
	kfree(parent);
	kfree(name);
	return 0;
}

int vfs_mkdir(const char* path, uint16_t mode)
{
	if (!path) {
		return -VFS_ERR_INVAL;
	}

	if (strcmp(path, "/") == 0) {
		return -VFS_ERR_EXIST;
	}

	char* norm_path = vfs_normalize_path(path, get_current_task()->cwd);
	if (!norm_path) {
		return -VFS_ERR_NOMEM;
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
		return -VFS_ERR_NOENT;
	}

	struct vfs_inode* pinode = pdentry->inode;

	// Optionally, check for existing child with the same name
	if (vfs_does_name_exist(pdentry, name)) {
		dput(pdentry);
		kfree(norm_path);
		kfree(parent);
		kfree(name);
		return -VFS_ERR_EXIST;
	}

	struct vfs_dentry* child = dentry_alloc(pdentry, name);
	if (!child) {
		dput(pdentry);
		kfree(norm_path);
		kfree(parent);
		kfree(name);
		return -VFS_ERR_NOMEM;
	}

	if (!pinode->ops || !pinode->ops->mkdir) {
		dput(pdentry);
		kfree(norm_path);
		kfree(parent);
		kfree(name);
		return -VFS_ERR_NODEV;
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
	return VFS_OK;
}

ssize_t __vfs_pwrite(struct vfs_file* file,
		     const char* buffer,
		     size_t count,
		     off_t* offset)
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

ssize_t
__vfs_pread(struct vfs_file* file, char* buffer, size_t count, off_t* offset)
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
	case SEEK_END:
		file->f_pos = (off_t)file->dentry->inode->f_size + offset;
		return file->f_pos;
	default: break;
	}

	return -EINVAL;
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
		dput(mount_point_dentry);
		return -VFS_ERR_NODEV; // The FS failed to mount
	}

	// The dentry for the mount point should now point to the new
	// superblock's root.
	struct vfs_inode* old = mount_point_dentry->inode;
	mount_point_dentry->inode = sb->root_dentry->inode;
	if (mount_point_dentry->inode) {
		// TODO: inode refcount api
		mount_point_dentry->inode->ref_count++;
	}
	if (old) {
		old->ref_count--;
	}
	// You'll also want to link the mount information so you can unmount it
	// later. Your vfs_mount struct is good for this.
	struct vfs_mount* new_mount =
		(struct vfs_mount*)kmalloc(sizeof(struct vfs_mount));
	new_mount->mount_point = strdup(target);
	sb->mount_point = new_mount->mount_point;
	new_mount->sb = sb;
	new_mount->flags = flags;
	register_mount(new_mount);

	dput(mount_point_dentry);
	log_info("Mounted %s on %s type %s", source, target, fstype);
	return VFS_OK; // Success!
}

struct vfs_dentry* vfs_lookup(const char* path)
{
	// If the VFS isn't even mounted, it's a fatal error.
	if (g_vfs_root_mount == nullptr) {
		panic("VFS lookup called before rootfs was mounted!");
	}

	// This can be called with no cwd early in boot
	struct task* t = get_current_task();
	struct vfs_dentry* base = t ? t->cwd :
				      g_vfs_root_mount->sb->root_dentry;
	char* norm_path = vfs_normalize_path(path, base);

	struct vfs_dentry* current_dentry =
		__vfs_walk_path(g_vfs_root_mount->sb->root_dentry, norm_path);

	kfree(norm_path);
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
		log_debug("Component %d: '%.*s'",
			  i,
			  (int)stack[i].len,
			  stack[i].start);
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

char* vfs_normalize_path(const char* path, struct vfs_dentry* base_dir)
{
	if (!base_dir || !base_dir->inode ||
	    base_dir->inode->filetype != FILETYPE_DIR) {
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
		log_debug("Component %d: '%.*s'",
			  i,
			  (int)stack[i].len,
			  stack[i].start);
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
int test_split_path()
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
		{ "/a/b/c",
		  VFS_OK,
		  "/",
		  "c" }, /* parent will be "/a/b" (verified below) */
		{ "/a/b//c///", VFS_OK, "/a/b", "c" },
		{ "a/b/c", VFS_OK, "a/b", "c" },
		{ "a////b", VFS_OK, "a", "b" },
		{ "/c", VFS_OK, "/", "c" },
		{ "c", VFS_OK, ".", "c" },
		{ "./a", VFS_OK, ".", "a" },
		{ "//a", VFS_OK, "/", "a" },
		{ "a/../b", VFS_OK, "a/..", "b" },
		{ "/.hidden", VFS_OK, "/", ".hidden" },

		/* --- Error cases --- */
		{ "", -VFS_ERR_INVAL, nullptr, nullptr },
		{ "/", -VFS_ERR_INVAL, nullptr, nullptr },
		{ "////", -VFS_ERR_INVAL, nullptr, nullptr },
		{ "a/.", -VFS_ERR_INVAL, nullptr, nullptr },
		{ "a/..", -VFS_ERR_INVAL, nullptr, nullptr },

		/* Additional edge-y successes */
		{ "a//", VFS_OK, ".", "a" },
		{ "///a///", VFS_OK, "/", "a" },
	};

	log_info(TESTING_HEADER, "Path Splitter");

	/* Run the table-driven tests. */
	for (size_t t = 0; t < sizeof(cases) / sizeof(cases[0]); ++t) {
		const struct test_case* tc = &cases[t];
		char* parent =
			(char*)0x1; /* sentinel non-nullptr so we can verify error paths null them */
		char* name = (char*)0x1;

		int rc = __split_path(tc->path, &parent, &name);
		++tests;

		if (tc->exp_rc == VFS_OK) {
			if (rc != VFS_OK) {
				log_error(
					"[T%zu] expected VFS_OK, got %d for path='%s'",
					t,
					rc,
					tc->path);
				++fails;
			}
			if (!parent || !name) {
				log_error(
					"[T%zu] outputs are nullptr on success for path='%s'",
					t,
					tc->path);
				++fails;
			} else {
				/* Parent can be more than just "/" or "."; check exact string expectations. */
				if (strcmp(tc->exp_parent, "/") == 0 &&
				    strcmp(tc->path, "/a/b/c") == 0) {
					/* Special verify for the first test: parent should be "/a/b". */
					if (strcmp(parent, "/a/b") != 0) {
						log_error(
							"[T%zu] parent mismatch path='%s' got='%s' want='/a/b'",
							t,
							tc->path,
							parent);
						++fails;
					}
				} else {
					if (strcmp(parent, tc->exp_parent) !=
					    0) {
						log_error(
							"[T%zu] parent mismatch path='%s' got='%s' want='%s'",
							t,
							tc->path,
							parent,
							tc->exp_parent);
						++fails;
					}
				}
				if (strcmp(name, tc->exp_name) != 0) {
					log_error(
						"[T%zu] name mismatch path='%s' got='%s' want='%s'",
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
				log_error(
					"[T%zu] expected rc=%d, got %d for path='%s'",
					t,
					tc->exp_rc,
					rc,
					tc->path);
				++fails;
			}
			if (parent != nullptr || name != nullptr) {
				log_error(
					"[T%zu] outputs must be nullptr on error for path='%s' (parent=%p, name=%p)",
					t,
					tc->path,
					(void*)parent,
					(void*)name);
				++fails;
				/* Defensive: avoid freeing sentinels. */
				if (parent && parent != (char*)0x1)
					kfree(parent);
				if (name && name != (char*)0x1) kfree(name);
			}
		}
	}

	/* ---- Length boundary tests for VFS_MAX_NAME ---- */

	/* Too-long name: "x/" + (VFS_MAX_NAME+1) of 'a' -> -VFS_ERR_NAMETOOLONG */
	{
		const size_t too_long = VFS_MAX_NAME + 1;
		char buf[VFS_MAX_NAME + 4 +
			 8]; /* "x/" + name + NUL; a little slack */
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
		if (rc != -VFS_ERR_NAMETOOLONG) {
			log_error(
				"[LEN1] expected -VFS_ERR_NAMETOOLONG, got %d for path of len=%zu",
				rc,
				strlen(buf));
			++fails;
		}
		if (parent != nullptr || name != nullptr) {
			log_error(
				"[LEN1] outputs must be nullptr on error (parent=%p, name=%p)",
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

		if (rc != VFS_OK) {
			log_error("[LEN2] expected VFS_OK, got %d", rc);
			++fails;
		} else {
			if (!parent || !name) {
				log_error(
					"[LEN2] outputs are nullptr on success");
				++fails;
			} else {
				if (strcmp(parent, "x") != 0) {
					log_error(
						"[LEN2] parent mismatch got='%s' want='x'",
						parent);
					++fails;
				}
				size_t nlen = strlen(name);
				if (nlen != VFS_MAX_NAME) {
					log_error(
						"[LEN2] name length mismatch got=%zu want=%zu",
						nlen,
						(size_t)VFS_MAX_NAME);
					++fails;
				}
			}
		}
		if (parent) kfree(parent);
		if (name) kfree(name);
	}

	log_info("parse_path_components: %zu/%zu tests passed",
		 tests - fails,
		 tests);

	kassert(fails == 0, "Some tests failed!");

	log_info(TESTING_FOOTER, "Path Splitter");

	return (int)fails;
}

/*******************************************************************************
 * Private Function Definitions
 *******************************************************************************/

/**
 * @brief Parse a filesystem path into parent directory and basename components.
 *
 * This function takes a canonical filesystem path and splits it into two parts:
 * - The *parent path* (e.g., `/usr/bin` from `/usr/bin/ls`)
 * - The *basename* (e.g., `ls` from `/usr/bin/ls`)
 *
 * Contract and Policy
 * - @p path must be a valid, null-terminated string.
 * - Trailing slashes are ignored (`/usr/bin/` → parent=`/usr`, name=`bin`).
 * - Multiple adjacent slashes are treated as a single separator.
 * - A root-only path (`/`) or all-slash input (`///`) is invalid.
 * - `.` and `..` are not valid basenames and will return `-VFS_ERR_INVAL`.
 * - The basename length must not exceed `VFS_MAX_NAME`, otherwise
 *   `-VFS_ERR_NAMETOOLONG` is returned.
 * - On success, both `parent_out` and `name_out` are allocated with `kzalloc`.
 *   The caller owns these buffers and must free them with `kfree()`.
 * - On allocation failure, both outputs are set to `nullptr` and
 *   `-VFS_ERR_NOMEM` is returned.
 * - On any failure, both `*parent_out` and `*name_out` are set to `nullptr`
 *   to ensure predictable cleanup behavior.
 *
 * Examples
 * | Input path       | parent_out | name_out | Return         |
 * |------------------|------------|----------|----------------|
 * | "/usr/bin/ls"    | "/usr/bin" | "ls"     | VFS_OK         |
 * | "foo/bar/"       | "foo"      | "bar"    | VFS_OK         |
 * | "/"              | nullptr    | nullptr  | -VFS_ERR_INVAL |
 * | "/.."            | nullptr    | nullptr  | -VFS_ERR_INVAL |
 * | "////"           | nullptr    | nullptr  | -VFS_ERR_INVAL |
 *
 * @param path       Input path string.
 * @param parent_out Pointer to receive allocated parent string.
 * @param name_out   Pointer to receive allocated basename string.
 *
 * @return VFS_OK on success, or a negative VFS_ERR_* code on error.
 */
static int __split_path(const char* path, char** parent_out, char** name_out)
{
	// TODO: Expect a normalized path, so we can just tokenize on '/'
	if (!path || !parent_out || !name_out) {
		return -VFS_ERR_INVAL;
	}

	const char* parent_begin;
	const char* name_begin;
	size_t parent_len;
	size_t name_len;
	size_t name_last;

	size_t path_len = strlen(path);
	if (path_len == 0) {
		*parent_out = *name_out = nullptr;
		return -VFS_ERR_INVAL;
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
		return -VFS_ERR_INVAL;
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
		return -VFS_ERR_NAMETOOLONG;
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
	if (name_begin[0] == '.' &&
	    (name_begin[1] == '.' || name_begin[1] == '\0')) {
		log_error("Invalid basename: '%s'", name_begin);
		*parent_out = *name_out = nullptr;
		return -VFS_ERR_INVAL;
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
		return -VFS_ERR_NOMEM;
	}

	memcpy(*parent_out, parent_begin, parent_len);
	memcpy(*name_out, name_begin, name_len);

	return VFS_OK;
}

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

// TODO: Make this use path_component struct
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
