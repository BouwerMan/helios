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

#include "fs/devfs/devfs.h"
#include <drivers/device.h>
#include <kernel/helios.h>
#include <lib/log.h>
#include <mm/kmalloc.h>

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

struct file_ops devfs_fops = {};

static struct sb_ops devfs_sb_ops = {};

/*******************************************************************************
 * Private Function Prototypes
 *******************************************************************************/

/**
 * Scans the device list for a device with the given name.
 */
static struct device* scan_devices(const char* name);
static struct vfs_inode* get_root_inode(struct vfs_superblock* sb);

/*******************************************************************************
 * Public Function Definitions
 *******************************************************************************/

void devfs_init()
{
	register_filesystem(&devfs_fs_type);
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

	info->next_inode_id = 1;
	info->flags = flags;
	hash_init(info->ht);

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

struct vfs_dentry* devfs_lookup(struct vfs_inode* dir_inode,
				struct vfs_dentry* child)
{
	log_debug("devfs_lookup: dir_inode=%p, child=%s",
		  (void*)dir_inode,
		  child->name);

	if (!dir_inode || dir_inode->filetype != FILETYPE_DIR) {
		return nullptr;
	}

	struct vfs_dentry* parent = child->parent;
	if (dir_inode != parent->inode) {
		return nullptr;
	}

	struct device* found = scan_devices(child->name);
	if (!found) {
		log_warn("Device '%s' not found", child->name);
		return nullptr; // Device not found
	}

	// child->inode = found->inode;
	struct vfs_inode* inode = devfs_alloc_inode(dir_inode->sb);
	if (!inode) {
		log_error("Failed to allocate inode for device '%s'",
			  child->name);
		return nullptr;
	}

	inode->id = DEVFS_SB_INFO(dir_inode->sb)->next_inode_id++;
	inode->filetype = FILETYPE_CHAR_DEV;
	inode->permissions = VFS_PERM_ALL; // TODO: Set appropriate perms
	inode->fops = found->fops;

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

/*******************************************************************************
 * Private Function Definitions
 *******************************************************************************/

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
	r_node->permissions =
		VFS_PERM_ALL; // TODO: use stricter perms once supported.
	r_node->flags = 0;

	// Add it to the cache so future lookups will find it.
	inode_add(r_node);

	return r_node;
}

static struct device* scan_devices(const char* name)
{
	struct device* child;
	list_for_each_entry (child, &g_registered_devices, list) {
		if (!strcmp(child->name, name)) {
			return child;
		}
	}
	return nullptr;
}
