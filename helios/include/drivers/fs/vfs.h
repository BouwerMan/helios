/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include <drivers/ata/controller.h>
#include <drivers/ata/device.h>
#include <drivers/ata/partition.h>

static constexpr size_t FS_TYPE_LEN = 8;
static constexpr size_t VFS_MAX_NAME = 255; // Not including null terminator

// TODO: Add more filesystems once drivers for them are supported
enum FILESYSTEMS {
	UNSUPPORTED = 0,
	RAMFS,
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

enum VFS_OPEN_FLAGS {
	O_RDONLY = 0x0000,  ///< Open for reading only
	O_WRONLY = 0x0001,  ///< Open for writing only
	O_RDWR = 0x0002,    ///< Open for reading and writing
	O_ACCMODE = 0x0003, ///< Mask for access mode (internal use)

	O_APPEND = 0x0004, ///< Writes append to the end of file
	O_CREAT = 0x0008,  ///< Create file if it does not exist
	O_TRUNC = 0x0010,  ///< Truncate file to zero length if it exists
	O_EXCL = 0x0020,   ///< Error if O_CREAT and file exists

	O_DIRECTORY = 0x0040, ///< Fail if the path is not a directory
	O_NOFOLLOW = 0x0080,  ///< Do not follow symlinks (when you support them)

	O_CLOEXEC = 0x0100, ///< Set close-on-exec (if you do exec)
};

enum VFS_SEEK_TYPES {
	SEEK_SET, // From beginning of file
	SEEK_CUR, // From f_pos
	SEEK_END, // From end of file
};

enum MOUNT_FLAGS {
	MOUNT_PRESENT = 0x1,
};

enum MOUNT_ERRORS {
	ENODEV,
	ENOENT,
	EFAULT,
};

/**
 * @brief Common error codes for VFS operations.
 */
enum vfs_err {
	VFS_OK = 0,	     ///< Success (no error)
	VFS_ERR_EXIST,	     ///< Entry already exists
	VFS_ERR_NOTDIR,	     ///< Not a directory
	VFS_ERR_NAMETOOLONG, ///< Name is too long
	VFS_ERR_NOENT,	     ///< No such file or directory
	VFS_ERR_NOSPC,	     ///< No space left on device
	VFS_ERR_NOMEM,	     ///< Out of memory
	VFS_ERR_PERM,	     ///< Permission denied
	VFS_ERR_IO,	     ///< I/O error (generic)
	VFS_ERR_NODEV,	     ///< No such device or device not available
	VFS_ERR_NOTEMPTY,    ///< Directory not empty
	VFS_ERR_ROFS,	     ///< Read-only file system
	VFS_ERR_FAULT,	     ///< Bad address (invalid pointer)
	VFS_ERR_BUSY,	     ///< Resource busy
	VFS_ERR_XDEV,	     ///< Cross-device link
	VFS_ERR_INVAL,	     ///< Invalid argument
	VFS_ERR_UNKNOWN,     ///< Miscellanious error
};

/**
 * Array of error names corresponding to VFS error codes.
 * The index of each error name matches the absolute value of the error code.
 */
static const char* vfs_err_names[] = { "VFS_OK",	 "VFS_ERR_EXIST", "VFS_ERR_NOTDIR",   "VFS_ERR_NAMETOOLONG",
				       "VFS_ERR_NOENT",	 "VFS_ERR_NOSPC", "VFS_ERR_NOMEM",    "VFS_ERR_PERM",
				       "VFS_ERR_IO",	 "VFS_ERR_NODEV", "VFS_ERR_NOTEMPTY", "VFS_ERR_ROFS",
				       "VFS_ERR_FAULT",	 "VFS_ERR_BUSY",  "VFS_ERR_XDEV",     "VFS_ERR_INVAL",
				       "VFS_ERR_UNKNOWN" };

/**
 * Retrieves the name of a VFS error code.
 * @param errno: The VFS error code (can be negative or positive).
 * @return: The corresponding error name as a string.
 */
static inline const char* vfs_get_err_name(enum vfs_err errno)
{
	if (errno < 0) errno = -errno;
	return vfs_err_names[errno];
}

// A more Unix-like vfs_file
struct vfs_file {
	struct vfs_dentry* dentry; // <-- THIS IS THE MAGIC LINK!
	off_t f_pos;		   // The current read/write offset for this session
	int flags;		   // Open flags (O_RDONLY, O_WRONLY, O_APPEND, etc.)
	int ref_count;		   // How many file descriptors point to this?
	struct file_ops* fops;
	void* private_data; // For filesystem-specific use
};

struct vfs_mount {
	char* mount_point;	   // Mount path, e.g. "/mnt/usb"
	struct vfs_superblock* sb; // Associated superblock
	sATADevice* device;	   // Optional: back-reference to device
	uint32_t lba_start;	   // Optional: offset of the partition
	int flags;		   // Optional: e.g., read-only
	struct vfs_mount* next;	   // Linked list of active mounts
	struct list_head* list;	   //Linked list of active mounts
};

// TODO: Add timestamp stuff
struct vfs_inode {
	size_t id;
	uint8_t filetype; // FILE, DIR, or maybe someday: CHAR_DEV, BLOCK_DEV, SYMLINK...
	size_t f_size;
	int ref_count;
	uint16_t permissions;
	uint8_t flags;
	struct inode_ops* ops; // What can you DO with this inode?
	struct file_ops* fops; // Default file ops

	struct vfs_superblock* sb; // A pointer back to the superblock of its filesystem
	uint32_t nlink;		   // Number of hard links (dentries) pointing to this inode

	void* fs_data; // Filesystem specific, for FAT it stores fat_inode_info
};

struct inode_ops {
	int (*mkdir)(struct vfs_inode* dir, struct vfs_dentry* dentry, uint16_t mode);
	int (*create)(struct vfs_inode* dir, struct vfs_dentry* dentry, uint16_t mode);

	// This is for navigating directories
	struct vfs_dentry* (*lookup)(struct vfs_inode* dir_inode, struct vfs_dentry* child);
};

struct file_ops {
	// These are for opening/closing the file handle
	int (*open)(struct vfs_inode* inode, struct vfs_file* file);
	int (*close)(struct vfs_inode* inode, struct vfs_file* file);

	// These are for I/O!
	ssize_t (*read)(struct vfs_file* file, char* buffer, size_t count);
	ssize_t (*write)(struct vfs_file* file, const char* buffer, size_t count);
};

// TODO: Make helper function for creating new dentries???
struct vfs_dentry {
	char* name;
	struct vfs_inode* inode;
	struct vfs_dentry* parent; // Reference to parent's directory

	struct list_head children; // Points to the *first child* in this directory
	struct list_head siblings; // Points to the *next child* in the parent's list

	struct hlist_node hash;	   /* list of hash table entries */
	struct hlist_head* bucket; /* hash bucket */

	void* fs_data; // Filesystem specific data, for FAT it stores fat_fs
	int ref_count;
	int flags;
};

struct vfs_fs_type {
	char fs_type[FS_TYPE_LEN]; // Filesystem name
	struct vfs_superblock* (*mount)(const char* source, int flags);
	struct vfs_fs_type* next;
};

struct vfs_superblock {
	struct vfs_dentry* root_dentry;
	struct vfs_fs_type* fs_type;
	void* fs_data;
	char* mount_point;
	struct sb_ops* sops;
};

struct sb_ops {
	struct inode* (*alloc_inode)(struct vfs_superblock* sb);
	void (*destroy_inode)(struct vfs_inode* inode);
};

void vfs_init();
int mount_initial_rootfs();

void register_filesystem(struct vfs_fs_type* fs);
struct vfs_dentry* vfs_lookup(const char* path);

int vfs_get_next_id();
int vfs_get_id();
void dentry_add(struct vfs_dentry* dentry);

struct vfs_superblock* vfs_get_sb(int idx);

struct vfs_dentry* dentry_lookup(struct vfs_dentry* parent, const char* name);
struct vfs_dentry* vfs_resolve_path(const char* path);
struct vfs_dentry* vfs_walk_path(struct vfs_dentry* root, const char* path);
struct vfs_dentry* dget(struct vfs_dentry* dentry);
void dput(struct vfs_dentry* dentry);

int vfs_open(const char* path, int flags);
int vfs_close(int fd);
ssize_t vfs_write(int fd, const char* buffer, size_t count);
ssize_t vfs_read(int fd, char* buffer, size_t count);
int vfs_mkdir(const char* path, uint16_t mode);
int vfs_create(const char* path, uint16_t mode, int flags, struct vfs_dentry** out_dentry);
off_t vfs_lseek(int fd, off_t offset, int whence);

struct vfs_file* get_file(int fd);

bool vfs_does_name_exist(struct vfs_dentry* parent, const char* name);
void vfs_dump_child(struct vfs_dentry* parent);
struct vfs_dentry* dentry_alloc(struct vfs_dentry* parent, const char* name);
void dentry_dealloc(struct vfs_dentry* d);
u32 dentry_hash(const struct vfs_dentry* key);
bool dentry_compare(const struct vfs_dentry* d1, const struct vfs_dentry* d2);
