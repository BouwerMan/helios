/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <drivers/fs/vfs.h>

struct ramfs_file {
	char* data;
	size_t capacity; // Allocated memory (most likely going to be page aligned)
	size_t size;	 // Actual size of file
};

// TODO: inode and dentry alloc and dealloc

void ramfs_init();
struct vfs_superblock* ramfs_mount(const char* source, int flags);
int ramfs_open(struct vfs_inode* inode, struct vfs_file* file);
int ramfs_close(struct vfs_inode* inode, struct vfs_file* file);
ssize_t ramfs_read(struct vfs_file* file, char* buffer, size_t count);
ssize_t ramfs_write(struct vfs_file* file, const char* buffer, size_t count);
struct vfs_dentry* ramfs_lookup(struct vfs_inode* dir_inode, struct vfs_dentry* child);
int ramfs_mkdir(struct vfs_inode* dir, struct vfs_dentry* dentry, uint16_t mode);
int ramfs_create(struct vfs_inode* dir, struct vfs_dentry* dentry, uint16_t mode);
