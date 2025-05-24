/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <drivers/ata/controller.h>
#include <drivers/ata/device.h>
#include <drivers/ata/partition.h>

// TODO: Add more filesystems once drivers for them are supported
enum FILESYSTEMS {
	UNSUPPORTED = 0,
	FAT16,
	FAT32, // Not sure these are techincally supported
	FAT12, // Not sure these are techincally supported
};

enum FILETYPE {
	FILETYPE_UNKNOWN,
	FILETYPE_FILE,
	FILETYPE_DIR,
};

enum DENTRY_FLAGS {
	DENTRY_NEGATIVE = 0x01, // NEEDED?
	DENTRY_DIR = 0x08,
	DENTRY_ROOT = 0x10,
};

enum VFS_PERMS {
	VFS_PERM_NONE = 0,

	// User permissions
	VFS_PERM_UR = 0b100000000, // User read
	VFS_PERM_UW = 0b010000000, // User write
	VFS_PERM_UX = 0b001000000, // User execute

	// Group permissions
	VFS_PERM_GR = 0b000100000, // Group read
	VFS_PERM_GW = 0b000010000, // Group write
	VFS_PERM_GX = 0b000001000, // Group execute

	// Other permissions
	VFS_PERM_OR = 0b000000100, // Other read
	VFS_PERM_OW = 0b000000010, // Other write
	VFS_PERM_OX = 0b000000001, // Other execute

	// Common combinations
	VFS_PERM_UALL = VFS_PERM_UR | VFS_PERM_UW | VFS_PERM_UX,
	VFS_PERM_GALL = VFS_PERM_GR | VFS_PERM_GW | VFS_PERM_GX,
	VFS_PERM_OALL = VFS_PERM_OR | VFS_PERM_OW | VFS_PERM_OX,
	VFS_PERM_ALL = VFS_PERM_UALL | VFS_PERM_GALL | VFS_PERM_OALL
};

enum MOUNT_FLAGS {
	MOUNT_PRESENT = 0x1,
};

struct vfs_file {
	void* file_ptr;
	unsigned char* read_ptr;
	size_t file_size;
};

struct vfs_mount {
	char* mount_point;	   // Mount path, e.g. "/mnt/usb"
	struct vfs_superblock* sb; // Associated superblock
	sATADevice* device;	   // Optional: back-reference to device
	uint32_t lba_start;	   // Optional: offset of the partition
	int flags;		   // Optional: e.g., read-only
	struct vfs_mount* next;	   // Linked list of active mounts
};

// TODO: Add timestamp stuff
struct vfs_inode {
	int id;
	uint8_t filetype; // either DIR or FILE
	size_t f_size;
	int ref_count;
	uint16_t permissions;
	uint8_t flags;
	struct inode_ops* ops;
	void* fs_data; // Filysystem specific, for FAT it stores fat_inode_info
};

struct inode_ops {
	int (*open)(struct vfs_inode* inode, struct vfs_file* file);
	int (*close)(struct vfs_inode* inode, struct vfs_file* file);
	struct vfs_dentry* (*lookup)(struct vfs_inode* dir_inode, struct vfs_dentry* child);
};

// TODO: Make helper function for creating new dentries???
struct vfs_dentry {
	char* name;
	struct vfs_inode* inode;
	struct vfs_dentry* parent; // Reference to parent's directory
	void* fs_data;		   // Filesystem specific data, for FAT it stores fat_fs
	int ref_count;
	int flags;
};

struct vfs_fs_type {
	char name[8];	 // Filesystem name
	uint8_t fs_type; // enum of filesystem type for faster checks
	struct vfs_superblock* (*mount)(sATADevice* device, uint32_t lba_start,
					int flags); // Mount function
	struct vfs_fs_type* next;
};

struct vfs_superblock {
	struct vfs_dentry* root_dentry;
	struct vfs_fs_type* fs_type;
	void* fs_data;
	char* mount_point;
};

void vfs_init(size_t dhash_size);
void register_filesystem(struct vfs_fs_type* fs);
int mount(const char* mount_point, sATADevice* device, sPartition* partition, uint8_t fs_type);

int vfs_get_next_id();
int vfs_get_id();
void dentry_add(struct vfs_dentry* dentry);
uint32_t dentry_hash(const void* key);
bool dentry_compare(const void* key1, const void* key2);

struct vfs_superblock* vfs_get_sb(int idx);

struct vfs_dentry* dentry_lookup(struct vfs_dentry* parent, const char* name);
struct vfs_dentry* vfs_resolve_path(const char* path);
struct vfs_dentry* vfs_walk_path(struct vfs_dentry* root, const char* path);
struct vfs_dentry* dget(struct vfs_dentry* dentry);

int vfs_open(const char* path, struct vfs_file* file);
void vfs_close(struct vfs_file* file);
