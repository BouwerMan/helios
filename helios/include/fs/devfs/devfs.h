/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "drivers/device.h"
#include "fs/vfs.h"
#include "lib/hashtable.h"

static constexpr size_t DEVFS_HASH_BITS = 9; // 512 buckets

// TODO: rwsem
struct devfs_sb_info {
	spinlock_t lock;
	size_t next_inode_id;
	int flags;
	struct list_head order; // stable iteration order for readdir
	DECLARE_HASHTABLE(buckets, DEVFS_HASH_BITS);
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

int devfs_readdir(struct vfs_file* file, struct dirent* dirent, off_t offset);

// Implement these for eager node creation later
// int devfs_create_node(const char* path,
// 		      int mode,
// 		      dev_t dev,
// 		      struct file_ops* fops);
// void devfs_remove_node(dev_t dev);

enum {
	DEVFS_F_REPLACE = 1 << 0, // overwrite existing mapping
};

struct devfs_entry {
	char* name;		 // basename only (no '/')
	size_t ino;		 // inode id
	dev_t rdev;		 // inode rdev
	u16 mode;		 // default perms (e.g., 0666)
	u16 type;		 // DEVFS_CHAR or DEVFS_BLOCK
	struct vfs_inode* inode; // optional inode cache (can be NULL)
	struct hlist_node hnode; // for buckets
	struct list_head olist;	 // for readdir order
};

// For now, we map name to dev_t. Must switch to devfs_create_node later
int devfs_map_name(struct vfs_superblock* sb,
		   const char* name,
		   dev_t rdev,
		   u16 type,
		   u16 mode,
		   unsigned flags);

int devfs_unmap_name(struct vfs_superblock* sb, const char* name);

int devfs_resolve_name(struct vfs_superblock* sb,
		       const char* name,
		       dev_t* out_rdev,
		       uint16_t* out_type,
		       uint16_t* out_mode,
		       struct devfs_entry** out_ent /* optional */);

int devfs_open(struct vfs_inode* inode, struct vfs_file* file);
int devnode_open(struct vfs_inode* inode, struct vfs_file* file);
