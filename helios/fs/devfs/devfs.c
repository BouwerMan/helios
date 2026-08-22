/**
 * @file drivers/fs/devfs.c
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

#include "drivers/device.h"
#include "fs/devfs/devfs.h"
#include "kernel/assert.h"
#include "kernel/spinlock.h"
#include "lib/hash.h"
#include "lib/list.h"
#include "lib/log.h"
#include "mm/kmalloc.h"

/**
 * @addtogroup fs
 * @{
 */

/*******************************************************************************
 * Global Variable Definitions
 *******************************************************************************/

extern struct list_head g_registered_devices;

struct vfs_fs_type devfs_fs_type = {
	.fs_type = "devfs",
	.mount = devfs_mount,
	.next = NULL,
};

struct inode_ops devfs_ops = {
	.lookup = devfs_lookup,
};

static struct file_ops devfs_fops = {
	.open = devfs_open,
	.readdir = devfs_readdir,
};

static struct sb_ops devfs_sb_ops = { };

/*******************************************************************************
 * Private Function Prototypes
 *******************************************************************************/

static struct vfs_inode* get_root_inode(struct vfs_superblock* sb);

static int __resolve_name(struct vfs_superblock* sb,
			  const char* name,
			  dev_t* out_rdev,
			  uint16_t* out_type,
			  uint16_t* out_mode,
			  struct devfs_entry** out_ent);

static inline u32 devfs_hash_name(const char* name)
{
	return hash_name32(name, DEVFS_HASH_BITS);
}

/*******************************************************************************
 * Public Function Definitions
 *******************************************************************************/

void devfs_init()
{
	register_filesystem(&devfs_fs_type);
	// TODO: Put this elsewhere?
	chrdevs_init();
}

struct vfs_superblock* devfs_mount(const char* source, int flags)
{
	log_debug("Mounting devfs with source: %s, flags: %d", source, flags);

	(void)source; // should always be nullptr for devfs

	struct vfs_superblock* sb = kzalloc(sizeof(struct vfs_superblock));
	if (!sb) {
		log_error("Failed to allocate superblock");
		return nullptr;
	}

	struct devfs_sb_info* info = kzalloc(sizeof(struct devfs_sb_info));
	if (!info) {
		log_error("Failed to allocate superblock info");
		goto clean_sb;
	}

	spin_init(&info->lock);
	info->next_inode_id = 1;
	info->flags = flags;
	list_init(&info->order);
	hash_init(info->buckets);

	sb->fs_data = info;

	struct vfs_dentry* root_dentry = dentry_alloc(nullptr, "/");
	if (!root_dentry) {
		log_error("Failed to allocate root dentry");
		goto clean_info;
	}

	root_dentry->flags = DENTRY_DIR | DENTRY_ROOT;

	root_dentry->inode = get_root_inode(sb);
	if (!root_dentry->inode) {
		log_error("Failed to allocate root inode");
		goto clean_dentry;
	}

	dentry_add(root_dentry);

	sb->root_dentry = root_dentry;
	sb->sops = &devfs_sb_ops;

	return sb;

clean_info:
	kfree(info);
clean_dentry:
	dentry_dealloc(root_dentry);
clean_sb:
	kfree(sb);
	return nullptr;
}

int devfs_readdir(struct vfs_file* file, struct dirent* dirent, off_t offset)
{
	if (!file || !dirent || offset < 0) {
		return -EINVAL;
	}

	struct vfs_dentry* pdentry = file->dentry;
	struct vfs_superblock* sb = pdentry->inode->sb;
	struct devfs_sb_info* info = DEVFS_SB_INFO(sb);

	spin_guard(&info->lock);

	off_t cur_off = 0;
	struct devfs_entry* entry;
	list_for_each_entry (entry, &info->order, olist) {
		if (cur_off++ < offset) {
			continue;
		}

		dirent->d_ino = entry->ino;

		switch (entry->type) {
		case FILETYPE_DIR:	dirent->d_type = DT_DIR; break;
		case FILETYPE_FILE:	dirent->d_type = DT_REG; break;
		case FILETYPE_CHAR_DEV: dirent->d_type = DT_CHR; break;
		default:		dirent->d_type = DT_UNKNOWN;
		}
		dirent->d_reclen = sizeof(struct dirent);

		strncpy(dirent->d_name, entry->name, 255);
		dirent->d_name[255] = '\0';

		dirent->d_off = cur_off + 1;
		return 1;
	}

	return 0;
}

struct vfs_dentry* devfs_lookup(struct vfs_inode* dir_inode, struct vfs_dentry* child)
{
	log_debug("devfs_lookup: dir_inode=%p, child=%s", (void*)dir_inode, child->name);

	if (!dir_inode || dir_inode->filetype != FILETYPE_DIR) {
		return nullptr;
	}

	struct vfs_dentry* parent = child->parent;
	if (dir_inode != parent->inode) {
		return nullptr;
	}

	struct vfs_inode* inode = devfs_alloc_inode(dir_inode->sb);
	if (!inode) {
		log_error("Failed to allocate inode for device '%s'", child->name);
		return nullptr;
	}
	struct devfs_sb_info* info = DEVFS_SB_INFO(dir_inode->sb);

	spin_guard(&info->lock);

	struct devfs_entry* entry;
	int rc = __resolve_name(dir_inode->sb, child->name, nullptr, nullptr, nullptr, &entry);
	if (rc < 0) {
		log_warn("Device '%s' not found in devfs", child->name);
		return nullptr; // Not found
	}

	if (entry->inode) {
		// Cached inode exists; reuse it.
		child->inode = entry->inode;
		dentry_add(child);
		return child;
	}

	inode->id = entry->ino;
	inode->filetype = (u8)entry->type;
	inode->rdev = entry->rdev;
	inode->permissions = entry->mode;
	entry->inode = iget(inode); // Cache it for next time

	child->inode = inode;
	dentry_add(child);
	return child;
}

/**
 * @brief Allocates a new in-memory inode for devfs.
 */
struct vfs_inode* devfs_alloc_inode(struct vfs_superblock* sb)
{
	(void)sb;

	struct vfs_inode* inode = kzalloc(sizeof(struct vfs_inode));
	if (!inode) {
		return nullptr;
	}

	// Set the operation pointers.
	inode->ops = &devfs_ops;
	inode->fops = &devfs_fops;

	sem_init(&inode->lock, 1);

	return inode;
}

/**
 * @brief Installs a /dev entry for a device number.
 *
 * @param sb devfs superblock. The mapping applies to one mount.
 * @param name Base name under /dev, with no slash, for example "ttyS0".
 * @param rdev Device number (major, minor) to store in inode->rdev.
 * @param type DEVFS_CHAR or DEVFS_BLOCK. Sets inode->filetype.
 * @param mode Default permissions, for example 0666.
 * @param flags DEVFS_F_REPLACE overwrites an existing entry. Otherwise the
 * function returns -EEXIST.
 *
 * @return 0 on success, -EINVAL for bad arguments, -EEXIST, or -ENOMEM.
 *
 * @note Does not create the inode immediately. devfs_lookup() creates and
 * caches it on first use.
 */
int devfs_map_name(struct vfs_superblock* sb, const char* name, dev_t rdev, u16 type, u16 mode, unsigned flags)
{
	if (!sb || rdev == 0) {
		return -EINVAL;
	}

	if (!name || !*name || strchr(name, '/')) {
		return -EINVAL;
	}

	if (chrdev_lookup(rdev, nullptr, nullptr, nullptr, nullptr) == -ENODEV) {
		// Device not registered
		log_warn("Mapping unregistered device %u,%u to /dev/%s", MAJOR(rdev), MINOR(rdev), name);
		return -EINVAL;
	}

	struct devfs_entry* entry = kzalloc(sizeof(struct devfs_entry));
	if (!entry) {
		return -ENOMEM;
	}

	entry->name = strdup(name);
	if (!entry->name) {
		kfree(entry);
		return -ENOMEM;
	}

	entry->rdev = rdev;
	entry->mode = mode;
	entry->type = type;

	struct devfs_sb_info* info = DEVFS_SB_INFO(sb);
	u32 hash = devfs_hash_name(name);

	spin_guard(&info->lock);

	if (__resolve_name(sb, name, nullptr, nullptr, nullptr, nullptr) == 0) {
		if (flags & DEVFS_F_REPLACE) {
			// TODO: REPLACE
			kunimpl("DEVFS_F_REPLACE");
		} else {
			kfree(entry->name);
			kfree(entry);
			return -EEXIST;
		}
	}

	entry->ino = info->next_inode_id++;
	hash_add(info->buckets, &entry->hnode, hash);
	list_add_tail(&info->order, &entry->olist);

	log_debug("Mapped device '%s' to %u,%u (ino %zu)",
		  entry->name,
		  MAJOR(entry->rdev),
		  MINOR(entry->rdev),
		  entry->ino);
	return 0;
}

/**
 * @brief Removes a /dev entry.
 *
 * @return 0 on success, or -ENOENT if the entry does not exist.
 *
 * @note If an inode is cached for this entry, invalidate or drop it here.
 */
int devfs_unmap_name(struct vfs_superblock* sb, const char* name)
{
	struct devfs_entry* entry;

	scoped_spin_guard(&DEVFS_SB_INFO(sb)->lock)
	{
		int rc = __resolve_name(sb, name, nullptr, nullptr, nullptr, &entry);
		if (rc < 0) {
			return rc;
		}

		hash_del(&entry->hnode);
		list_del(&entry->olist);
	}

	if (!entry->inode) {
		// TODO: More aggressive cleanup if cached inode exists
		iput(entry->inode);
	}

	kfree(entry->name);
	kfree(entry);

	return 0;
}

/**
 * @brief Resolves a name to (rdev, type, mode) for devfs_lookup().
 *
 * @return 0 on success, or -ENOENT if the entry does not exist.
 *
 * @note Optionally returns the entry so the caller can reuse its cached
 * inode.
 */
int devfs_resolve_name(struct vfs_superblock* sb,
		       const char* name,
		       dev_t* out_rdev,
		       uint16_t* out_type,
		       uint16_t* out_mode,
		       struct devfs_entry** out_ent)
{
	if (!sb || !name) {
		return -ENOENT;
	}

	struct devfs_sb_info* info = DEVFS_SB_INFO(sb);

	spin_guard(&info->lock);

	int rc = __resolve_name(sb, name, out_rdev, out_type, out_mode, out_ent);

	return rc;
}

/**
 * @brief Opens a devfs inode.
 *
 * @param inode Inode to open.
 * @param file VFS file being initialized.
 *
 * @return 0 on success, or a negative errno on failure.
 *
 * @note May sleep. The caller holds no devfs locks.
 */
int devfs_open(struct vfs_inode* inode, struct vfs_file* file)
{
	switch (inode->filetype) {
	case FILETYPE_CHAR_DEV: return devnode_open(inode, file);
	case FILETYPE_DIR:	{
		if (inode->sb->root_dentry->inode == inode) {
			// Opening root dir is always OK
			return 0;
		}
		__fallthrough;
	}
	default: return -ENOSYS;
	}
}

/**
 * @brief Binds a devfs character device to a file.
 *
 * @param inode Device inode with type FILETYPE_CHAR_DEV.
 * @param file VFS file to attach ops and private data to.
 *
 * @return 0 on success, -EINVAL for bad arguments, or an error from the
 * registry lookup.
 *
 * @note May sleep. The caller holds no devfs locks.
 *
 * Looks up the registered character device driver and installs its file
 * operations and private data on @file.
 */
int devnode_open(struct vfs_inode* inode, struct vfs_file* file)
{
	if (!inode || !file) {
		return -EINVAL;
	}

	log_debug("Opening device inode %zu (rdev=%u,%u)", inode->id, MAJOR(inode->rdev), MINOR(inode->rdev));
	log_debug("File name: %s", file->dentry ? file->dentry->name : "<null>");

	dev_t dev = inode->rdev;

	struct file_ops* fops = nullptr;
	void* drv = nullptr;
	int rc = chrdev_lookup(dev, (const struct file_ops**)&fops, &drv, nullptr, nullptr);
	if (rc) {
		log_warn("Could not find chrdev for device %u,%u: %s",
			 MAJOR(dev),
			 MINOR(dev),
			 rc == -ENODEV ? "not registered" : "error");
		return rc;
	}

	file->fops = fops;
	file->private_data = drv;

	if (file->fops && file->fops->open) {
		return file->fops->open(inode, file);
	}

	return 0;
}

/*******************************************************************************
 * Private Function Definitions
 *******************************************************************************/

/**
 * @brief Looks up a devfs entry by name.
 *
 * @param sb Superblock owning the devfs instance.
 * @param name NUL-terminated entry name.
 * @param out_rdev Optional output device number.
 * @param out_type Optional output FILETYPE_* value.
 * @param out_mode Optional output permission mode.
 * @param out_ent Optional output pointer to the entry.
 *
 * @return 0 on success, or -ENOENT if not found.
 *
 * @note Caller must hold DEVFS_SB_INFO(@sb)->lock. Does not sleep.
 */
static int __resolve_name(struct vfs_superblock* sb,
			  const char* name,
			  dev_t* out_rdev,
			  uint16_t* out_type,
			  uint16_t* out_mode,
			  struct devfs_entry** out_ent)
{
	struct devfs_sb_info* info = DEVFS_SB_INFO(sb);

	u32 hash = devfs_hash_name(name);
	struct devfs_entry* entry;
	hash_for_each_possible (info->buckets, entry, hnode, hash) {
		if (strcmp(name, entry->name) != 0) {
			continue;
		}

		if (out_rdev) {
			*out_rdev = entry->rdev;
		}
		if (out_type) {
			*out_type = entry->type;
		}
		if (out_mode) {
			*out_mode = entry->mode;
		}
		if (out_ent) {
			*out_ent = entry;
		}

		return 0;
	}

	return -ENOENT;
}

/**
 * @brief Creates and caches the devfs root inode.
 *
 * @param sb Superblock that owns the root inode.
 *
 * @return New root inode on success, or NULL on failure.
 *
 * @note May sleep. The caller holds no devfs locks.
 */
static struct vfs_inode* get_root_inode(struct vfs_superblock* sb)
{
	if (!sb) return nullptr;

	struct vfs_inode* r_node = devfs_alloc_inode(sb);
	if (!r_node) {
		log_error("Failed to allocate root inode");
		return nullptr;
	}

	r_node->sb = sb;
	r_node->id = 0;
	r_node->ref_count = 1;

	r_node->filetype = FILETYPE_DIR;
	r_node->permissions = VFS_PERM_ALL; // TODO: use stricter perms once supported.
	r_node->flags = 0;

	// Add it to the cache so future lookups will find it.
	inode_add(r_node);

	return r_node;
}

/** @} */
