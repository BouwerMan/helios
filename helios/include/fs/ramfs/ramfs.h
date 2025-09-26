/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "fs/vfs.h"
#include <lib/hashtable.h>

// TODO: Make sure when things are fully deallocated we can still find the data later.

static constexpr size_t RAMFS_HASH_BITS = 9; // 512 buckets
static constexpr size_t RAMFS_MAX_NAME = 31;

// TODO: Enforce name length or make dynamic
struct ramfs_dentry {
	char name[RAMFS_MAX_NAME + 1];	     // Name of the file/directory
	struct ramfs_inode_info* inode_info; // Pointer to the inode info

	struct list_head children; // List of child dentries (files/directories)
	struct list_head siblings; // Sibling directories
};

struct ramfs_file {
	char* data;
	size_t capacity; // Allocated memory (most likely going to be page
			 // aligned)
	size_t size;	 // Actual size of file
};

/**
 * Private inode information for ramfs.
 * It will be persistant in memory so we can re-open files after inode deallocation.
 */
struct ramfs_inode_info {
	size_t id; // Unique identifier for the inode
	uint16_t permissions;
	uint8_t flags;
	uint8_t filetype;
	size_t f_size;

	struct hlist_node hash;	   /* list of hash table entries */
	struct hlist_head* bucket; /* hash bucket */

	union {
		struct ramfs_file* file;
	};
};

struct ramfs_sb_info {
	struct ramfs_dentry* root;
	size_t next_inode_id;
	int flags;
	DECLARE_HASHTABLE(ht, RAMFS_HASH_BITS);
};

static inline struct ramfs_inode_info* RAMFS_INODE_INFO(struct vfs_inode* inode)
{
	return inode->fs_data ? (struct ramfs_inode_info*)inode->fs_data :
				nullptr;
}

static inline struct ramfs_file* RAMFS_FILE(struct vfs_inode* inode)
{
	return RAMFS_INODE_INFO(inode) ? RAMFS_INODE_INFO(inode)->file :
					 nullptr;
}

static inline struct ramfs_dentry* RAMFS_DENTRY(struct vfs_dentry* dentry)
{
	return dentry ? (struct ramfs_dentry*)dentry->fs_data : nullptr;
}

// static inline struct ramfs_dir* RAMFS_DIR(struct vfs_inode* inode)
// {
// 	return RAMFS_INODE_INFO(inode) ? RAMFS_INODE_INFO(inode)->dir : nullptr;
// }

static inline struct ramfs_sb_info* RAMFS_SB_INFO(struct vfs_superblock* sb)
{
	return sb->fs_data ? (struct ramfs_sb_info*)sb->fs_data : nullptr;
}

void ramfs_init();
struct vfs_superblock* ramfs_mount(const char* source, int flags);
int ramfs_open(struct vfs_inode* inode, struct vfs_file* file);
int ramfs_close(struct vfs_inode* inode, struct vfs_file* file);

ssize_t
ramfs_read(struct vfs_file* file, char* buffer, size_t count, off_t* offset);
ssize_t ramfs_write(struct vfs_file* file,
		    const char* buffer,
		    size_t count,
		    off_t* offset);

int ramfs_readdir(struct vfs_file* file, struct dirent* dirent, off_t offset);
int ramfs_readpage(struct vfs_inode* inode, struct page* page);

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

void ramfs_test();
