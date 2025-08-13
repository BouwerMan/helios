/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <drivers/fs/vfs.h>
#include <util/hashtable.h>

static constexpr size_t DEVFS_HASH_BITS = 9; // 512 buckets

struct devfs_sb_info {
	size_t next_inode_id;
	int flags;
	DECLARE_HASHTABLE(ht, DEVFS_HASH_BITS);
};

static inline struct devfs_sb_info* DEVFS_SB_INFO(struct vfs_superblock* sb)
{
	return sb->fs_data ? (struct devfs_sb_info*)sb->fs_data : nullptr;
}

void devfs_init();
struct vfs_superblock* devfs_mount(const char* source, int flags);
struct vfs_dentry* devfs_lookup(struct vfs_inode* dir_inode,
				struct vfs_dentry* child);

struct vfs_inode* devfs_alloc_inode(struct vfs_superblock* sb);
