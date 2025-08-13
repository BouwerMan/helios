/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <drivers/fs/vfs.h>
#include <util/hashtable.h>

static constexpr size_t RAMFS_HASH_BITS = 9; // 512 buckets

struct ramfs_file {
	char* data;
	size_t capacity; // Allocated memory (most likely going to be page
			 // aligned)
	size_t size;	 // Actual size of file
};

struct ramfs_inode_info {
	size_t id; // Unique identifier for the inode
	uint16_t permissions;
	uint8_t flags;
	uint8_t filetype;

	struct hlist_node hash;	   /* list of hash table entries */
	struct hlist_head* bucket; /* hash bucket */

	// For a file, this would point to the ramfs_file struct
	// For a directory, it could be null or point to a list of children
	union {
		struct ramfs_file* file;
		// struct ramfs_dir* dir;
	};
};

struct ramfs_sb_info {
	size_t next_inode_id;
	int flags;
	DECLARE_HASHTABLE(ht, RAMFS_HASH_BITS);
};

static inline struct ramfs_file* RAMFS_FILE(struct vfs_inode* inode)
{
	return inode->fs_data ?
		       ((struct ramfs_inode_info*)inode->fs_data)->file :
		       nullptr;
}

static inline struct ramfs_sb_info* RAMFS_SB_INFO(struct vfs_superblock* sb)
{
	return sb->fs_data ? (struct ramfs_sb_info*)sb->fs_data : nullptr;
}

void ramfs_init();
struct vfs_superblock* ramfs_mount(const char* source, int flags);
int ramfs_open(struct vfs_inode* inode, struct vfs_file* file);
int ramfs_close(struct vfs_inode* inode, struct vfs_file* file);
ssize_t ramfs_read(struct vfs_file* file, char* buffer, size_t count);
ssize_t ramfs_write(struct vfs_file* file, const char* buffer, size_t count);
struct vfs_dentry* ramfs_lookup(struct vfs_inode* dir_inode,
				struct vfs_dentry* child);
int ramfs_mkdir(struct vfs_inode* dir,
		struct vfs_dentry* dentry,
		uint16_t mode);
int ramfs_create(struct vfs_inode* dir,
		 struct vfs_dentry* dentry,
		 uint16_t mode);
struct vfs_inode* ramfs_alloc_inode(struct vfs_superblock* sb);
void ramfs_destroy_inode(struct vfs_inode* inode);
int ramfs_read_inode(struct vfs_inode* inode);
