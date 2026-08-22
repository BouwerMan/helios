/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "drivers/device.h"
#include "fs/imapping.h"
#include <drivers/ata/controller.h>
#include <drivers/ata/device.h>
#include <drivers/ata/partition.h>
#include <kernel/semaphores.h>
#include <kernel/types.h>
#include <kernel/uaccess.h>
#include <lib/hashtable.h>
#include <mm/page.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <uapi/helios/dirent.h>
#include <uapi/helios/fcntl.h>

/**
 * @addtogroup fs
 * @{
 */

static constexpr size_t FS_TYPE_LEN = 8;
static constexpr size_t VFS_MAX_NAME = 255; // Not including null terminator
static constexpr size_t VFS_MAX_PATH = 255; // Not inlcuding null terminator

// TODO: Add more filesystems once drivers for them are supported
/**
 * @brief Filesystem types recognized by register_filesystem() and
 * vfs_mount().
 */
enum FILESYSTEMS {
	UNSUPPORTED = 0, /**< No filesystem, or an unrecognized fs_type string. */
	RAMFS,		 /**< In-memory filesystem. */
	FAT16,		 /**< FAT16. */
	FAT32,		 /**< FAT32. Support is unverified. */
	FAT12,		 /**< FAT12. Support is unverified. */
};

/**
 * @brief Kind of file a vfs_inode represents.
 */
enum FILETYPE {
	FILETYPE_UNKNOWN,  /**< Not yet determined. */
	FILETYPE_FILE,	   /**< A regular file. */
	FILETYPE_DIR,	   /**< A directory. */
	FILETYPE_CHAR_DEV, /**< A character device node. */
};

/**
 * @brief Flags stored in vfs_dentry::flags.
 */
enum DENTRY_FLAGS {
	/** The dentry has no inode: a cached lookup miss. */ // NEEDED?
	DENTRY_NEGATIVE = 0x01,
	DENTRY_DIR = 0x08,				      /**< The dentry names a directory. */
	DENTRY_ROOT = 0x10,				      /**< The dentry is a filesystem's root. */
};

/**
 * @brief Unix-style permission bits stored in vfs_inode::permissions.
 */
enum VFS_PERMS {
	VFS_PERM_NONE = 0, /**< No permissions. */

	// User permissions
	VFS_PERM_UR = 0b100000000, /**< User read. */
	VFS_PERM_UW = 0b010000000, /**< User write. */
	VFS_PERM_UX = 0b001000000, /**< User execute. */

	// Group permissions
	VFS_PERM_GR = 0b000100000, /**< Group read. */
	VFS_PERM_GW = 0b000010000, /**< Group write. */
	VFS_PERM_GX = 0b000001000, /**< Group execute. */

	// Other permissions
	VFS_PERM_OR = 0b000000100, /**< Other read. */
	VFS_PERM_OW = 0b000000010, /**< Other write. */
	VFS_PERM_OX = 0b000000001, /**< Other execute. */

	// Common combinations
	VFS_PERM_UALL = VFS_PERM_UR | VFS_PERM_UW | VFS_PERM_UX,     /**< All user permissions. */
	VFS_PERM_GALL = VFS_PERM_GR | VFS_PERM_GW | VFS_PERM_GX,     /**< All group permissions. */
	VFS_PERM_OALL = VFS_PERM_OR | VFS_PERM_OW | VFS_PERM_OX,     /**< All other permissions. */
	VFS_PERM_ALL = VFS_PERM_UALL | VFS_PERM_GALL | VFS_PERM_OALL /**< All permissions. */
};

#ifndef SEEK_SET
/**
 * @brief Reference point for vfs_lseek().
 */
enum VFS_SEEK_TYPES {
	SEEK_SET, /**< Offset is relative to the start of the file. */
	SEEK_CUR, /**< Offset is relative to the file's current position. */
	SEEK_END, /**< Offset is relative to the end of the file. */
};
#endif

/**
 * @brief Flags stored in vfs_mount::flags.
 */
enum MOUNT_FLAGS {
	MOUNT_PRESENT = 0x1, /**< The mount slot is in use. */
};

/**
 * @brief An open file: the connection between a file descriptor and a
 * dentry.
 *
 * One vfs_file is shared by every file descriptor created from the
 * same open() call (e.g. via dup()); ref_count tracks how many.
 */
struct vfs_file {
	struct vfs_dentry* dentry; /**< The file this descriptor refers to. */
	off_t f_pos;		   /**< Current read/write offset for this session. */
	int flags;		   /**< Open flags: O_RDONLY, O_WRONLY, O_APPEND, etc. */
	int ref_count;		   /**< Number of file descriptors that point to this. */
	struct file_ops* fops;	   /**< Operations for this file. Usually inode->fops. */
	void* private_data;	   /**< Filesystem-specific use. */
};

/**
 * @brief An active mount point.
 */
struct vfs_mount {
	char* mount_point;	   /**< Mount path, e.g. "/mnt/usb". */
	struct vfs_superblock* sb; /**< The mounted filesystem's superblock. */
	sATADevice* device;	   /**< Backing device, if any. */
	uint32_t lba_start;	   /**< Partition offset on device, if any. */
	int flags;		   /**< E.g. read-only. */
	struct vfs_mount* next;	   /**< Next entry in the list of active mounts. */
	struct list_head* list;	   /**< Node in the list of active mounts. */
};

// TODO: Add timestamp stuff
/**
 * @brief An in-memory file, directory, or device node.
 *
 * One vfs_inode exists per unique file per superblock, shared by every
 * vfs_dentry that names it (nlink counts those hard links).
 */
struct vfs_inode {
	size_t id;		       /**< Filesystem-specific inode number. */
	uint8_t filetype;	       /**< FILETYPE_FILE, FILETYPE_DIR, or FILETYPE_CHAR_DEV. */
	size_t f_size;		       /**< File size in bytes. */
	int ref_count;		       /**< Number of live references. See iget()/iput(). */
	uint16_t permissions;	       /**< VFS_PERM_* bits. */
	uint8_t flags;		       /**< Inode flags. */
	dev_t rdev;		       /**< Device number, for device files. */
	semaphore_t lock;	       /**< Locks this inode's fields. */
	struct inode_mapping* mapping; /**< This inode's page cache. */
	struct inode_ops* ops;	       /**< Directory operations: mkdir, create, lookup. */
	struct file_ops* fops;	       /**< Default file operations for this inode. */

	struct vfs_superblock* sb;     /**< The superblock of this inode's filesystem. */

	struct hlist_node hash;	       /**< Node in the inode hash table. */
	struct hlist_head* bucket;     /**< The hash table bucket that owns this node. */

	uint32_t nlink;		       /**< Number of dentries (hard links) that name this inode. */

	void* fs_data;		       /**< Filesystem-specific data, e.g. fat_inode_info for FAT. */
};

/**
 * @brief Directory operations implemented by a filesystem driver.
 */
struct inode_ops {
	int (*mkdir)(struct vfs_inode* dir, struct vfs_dentry* dentry, uint16_t mode);	/**< Creates a subdirectory. */
	int (*create)(struct vfs_inode* dir, struct vfs_dentry* dentry, uint16_t mode); /**< Creates a file. */

	/** Resolves a name within a directory. */
	struct vfs_dentry* (*lookup)(struct vfs_inode* dir_inode, struct vfs_dentry* child);
};

/**
 * @brief File operations implemented by a filesystem or device driver.
 */
struct file_ops {
	int (*open)(struct vfs_inode* inode, struct vfs_file* file);  /**< Called when the file is opened. */
	int (*close)(struct vfs_inode* inode, struct vfs_file* file); /**< Called when the file is closed. */

	ssize_t (*read)(struct vfs_file* file, char* buffer, size_t count, off_t* offset); /**< Reads bytes. */

	/** Writes bytes. */
	ssize_t (*write)(struct vfs_file* file, const char __user* buffer, size_t count, off_t* offset);

	int (*readdir)(struct vfs_file* file, struct dirent* dirent, off_t offset);   /**< Reads one directory entry. */

	int (*ioctl)(struct vfs_file* file, unsigned long request, void __user* arg); /**< Device-specific control. */

	short (*poll)(struct vfs_file* file); /**< Reports readiness for read/write. */

	/** Maps the file into a virtual address space. */
	int (*mmap)(struct vfs_file* file, void* addr, size_t len, int prot, int flags, off_t off);
};

// TODO: Make helper function for creating new dentries???
/**
 * @brief A named entry in the directory tree: maps a name to an
 * inode.
 *
 * @note See "Dentry lifetime & refs" below, and
 * docs/man9/vfs_dentry_refs.9.md, for reference-counting rules.
 */
struct vfs_dentry {
	char* name;		   /**< This entry's name within its parent directory. */
	struct vfs_inode* inode;   /**< The inode this name refers to. nullptr for a negative entry. */
	struct vfs_dentry* parent; /**< Parent directory's dentry. */

	struct list_head children; /**< First child, if this dentry is a directory. */
	struct list_head siblings; /**< Next child in the parent's children list. */

	struct hlist_node hash;	   /**< Node in the dentry hash table. */
	struct hlist_head* bucket; /**< The hash table bucket that owns this node. */

	void* fs_data;		   /**< Filesystem-specific data, e.g. fat_fs for FAT. */
	int ref_count;		   /**< Number of live references. See dget()/dput(). */
	int flags;		   /**< DENTRY_* flags. */
};

/**
 * @brief Registers a filesystem driver with the VFS.
 */
struct vfs_fs_type {
	char fs_type[FS_TYPE_LEN]; /**< Filesystem name, matched against the mount() fstype argument. */
	struct vfs_superblock* (*mount)(const char* source, int flags); /**< Mounts an instance of this filesystem. */
	struct vfs_fs_type* next; /**< Next entry in the list of registered filesystems. */
};

/**
 * @brief A mounted filesystem instance.
 */
struct vfs_superblock {
	struct vfs_dentry* root_dentry; /**< The filesystem's root directory. */
	struct vfs_fs_type* fs_type;	/**< The filesystem driver that owns this superblock. */
	char* mount_point;		/**< Path this filesystem is mounted at. */
	struct sb_ops* sops;		/**< Superblock operations. */
	void* fs_data;			/**< Filesystem-specific data. */
};

/**
 * @brief Superblock operations implemented by a filesystem driver.
 */
struct sb_ops {
	struct vfs_inode* (*alloc_inode)(struct vfs_superblock* sb); /**< Allocates an uninitialized inode. */
	void (*destroy_inode)(struct vfs_inode* inode);		     /**< Frees an inode allocated by alloc_inode. */
	int (*read_inode)(struct vfs_inode* inode); /**< Fills in an inode's fields from the filesystem's on-disk data.
						     */
};

/**
 * @brief Gets the inode an open file refers to.
 *
 * @param file The open file.
 *
 * @return The file's inode, or nullptr if file has no dentry.
 */
static inline struct vfs_inode* inode_from_file(struct vfs_file* file)
{
	return file->dentry ? file->dentry->inode : nullptr;
}

// --- VFS Initialization and Mounting ---
void vfs_init();
int mount_initial_rootfs();
int vfs_mount(const char* source, const char* target, const char* fstype, int flags);
void register_filesystem(struct vfs_fs_type* fs);
struct vfs_superblock* vfs_get_sb(const char* path);

// --- Path and Dentry Management ---

/*
 * DOC: Dentry lifetime & refs
 * Dentries are owned via counted references. Key rules:
 * 1) Returning cached dentries: dget() before returning.
 * 2) Fresh from dentry_alloc(): do not add another ref.
 * 3) Walkers hold exactly one ref to the current component.
 * 4) Balance refs on all success/error paths (dput() on failure).
 * 5) dentry_add() takes a cache ref; caller still owns exactly one.
 * 6) Last dput() frees the dentry and iput()s the inode; never touch after.
 * NOTE: __* helpers are internal; require normalized paths/preconditions.
 * See: docs/man9/vfs_dentry_refs.9.md
 */
struct vfs_dentry* vfs_lookup(const char* path);
int vfs_access(const char* path, int amode);
struct vfs_dentry* vfs_resolve_path(const char* path);
struct vfs_dentry* __vfs_walk_path(struct vfs_dentry* root, const char* path);
struct vfs_dentry* __dentry_lookup(struct vfs_dentry* parent, const char* name);
struct vfs_dentry* dget(struct vfs_dentry* dentry);
void dput(struct vfs_dentry* dentry);
void dentry_add(struct vfs_dentry* dentry);
struct vfs_dentry* dentry_alloc(struct vfs_dentry* parent, const char* name);
void dentry_dealloc(struct vfs_dentry* d);
void register_child(struct vfs_dentry* parent, struct vfs_dentry* child);
int __fill_dirent(struct vfs_dentry* dentry, struct dirent* dirent);
struct vfs_dentry* vfs_resolve_path_from_cwd(const char* path, struct vfs_dentry* cwd);

// --- Inode Management ---
struct vfs_inode* new_inode(struct vfs_superblock* sb, size_t id);
struct vfs_inode* iget(struct vfs_inode* inode);
void iput(struct vfs_inode* inode);
void inode_add(struct vfs_inode* inode);

// --- File Operations (Syscall Layer) ---
int vfs_open(const char* path, int flags);
int __vfs_open_for_task(struct task* t, const char* path, int flags);
int vfs_close(int fd);
ssize_t vfs_getdents(struct vfs_file* dir, struct dirent* dirp, size_t count);
int vfs_readdir(struct vfs_file* dir, struct dirent* out, long pos);

ssize_t __vfs_pwrite(struct vfs_file* file, const char* buffer, size_t count, off_t* offset);
ssize_t vfs_pwrite(int fd, const char* buffer, size_t count, off_t offset);
ssize_t vfs_file_write(struct vfs_file* file, const char* buffer, size_t count);
ssize_t vfs_write(int fd, const char* buffer, size_t count);

ssize_t __vfs_pread(struct vfs_file* file, char* buffer, size_t count, off_t* offset);
ssize_t vfs_pread(int fd, char* buffer, size_t count, off_t offset);
ssize_t vfs_file_read(struct vfs_file* file, char* buffer, size_t count);
ssize_t vfs_read(int fd, char* buffer, size_t count);

off_t vfs_lseek(int fd, off_t offset, int whence);
int vfs_mkdir(const char* path, uint16_t mode);
int vfs_create(const char* path, uint16_t mode, int flags, struct vfs_dentry** out_dentry);

// --- Utility Functions ---
char* dentry_to_abspath(struct vfs_dentry* dentry);
char* vfs_normalize_path(const char* path, struct vfs_dentry* base_dir);
struct vfs_file* get_file(int fd);
bool vfs_does_name_exist(struct vfs_dentry* parent, const char* name);
void vfs_dump_child(struct vfs_dentry* parent);
u32 dentry_hash(const struct vfs_dentry* key);
struct vfs_inode* inode_ht_check(struct vfs_superblock* sb, size_t id);
bool dentry_compare(const struct vfs_dentry* d1, const struct vfs_dentry* d2);
int vfs_get_next_id();
int vfs_get_id();

/** @} */
