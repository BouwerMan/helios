/**
 * @file drivers/fs/ramfs.c
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

#undef LOG_LEVEL
#define LOG_LEVEL 0
#define FORCE_LOG_REDEF
#include "lib/log.h"
#undef FORCE_LOG_REDEF

#include "fs/ramfs/ramfs.h"
#include "fs/vfs.h"
#include "kernel/panic.h"
#include "lib/hashtable.h"
#include "lib/string.h"
#include "mm/kmalloc.h"
#include "mm/page.h"
#include "mm/page_alloc.h"
#include "mm/slab.h"

#include <uapi/helios/errno.h>

// TODO: Locking

/*******************************************************************************
 * Global Variable Definitions
 *******************************************************************************/

struct vfs_fs_type ramfs_fs_type = {
	.fs_type = "ramfs",
	.mount = ramfs_mount,
	.next = NULL,
};

struct inode_ops ramfs_ops = {
	.lookup = ramfs_lookup,
	.mkdir = ramfs_mkdir,
	.create = ramfs_create,
};

struct file_ops ramfs_fops = {
	.write = ramfs_write,
	.read = ramfs_read,
	.open = ramfs_open,
	.close = ramfs_close,
	.readdir = ramfs_readdir,
};

static struct sb_ops ramfs_sb_ops = {
	.alloc_inode = ramfs_alloc_inode,
	.destroy_inode = ramfs_destroy_inode,
};

static struct inode_mapping_ops ramfs_imops = {
	.readpage = ramfs_readpage,
	// .writepage = ramfs_writepage, // You can add this later
};

/*******************************************************************************
 * Private Function Prototypes
 *******************************************************************************/

/**
 * Scans dir for name in it's child list.
 */
static struct ramfs_dentry* scan_dir(struct ramfs_dentry* dir,
				     const char* name);
static bool does_name_exist(struct ramfs_dentry* dir, const char* name);
static struct vfs_inode* get_root_inode(struct vfs_superblock* sb);
static int add_child_to_list(struct ramfs_dentry* parent,
			     struct ramfs_dentry* child);
/**
 * Finds inode by id in the hashtable.
 */
static struct ramfs_inode_info* find_private_inode(struct vfs_superblock* sb,
						   size_t id);

/**
 * Adds info to the hashtable.
 */
static void info_add(struct vfs_superblock* sb, struct ramfs_inode_info* info);

/**
 * Syncronizes the inode's state with the filesystem's private data.
 */
static void sync_to_inode(struct vfs_inode* inode);
static void sync_to_info(struct vfs_inode* inode);

/**
 * Just allocates main inode and op pointers.
 */
static struct vfs_inode* _alloc_inode_raw(struct vfs_superblock* sb);

static struct vfs_dentry* find_child(struct vfs_dentry* parent,
				     const char* name);

/*******************************************************************************
 * Public Function Definitions
 *******************************************************************************/

/**
 * @brief Initializes the ramfs filesystem driver.
 */
void ramfs_init()
{
	register_filesystem(&ramfs_fs_type);
}

struct vfs_superblock* ramfs_mount(const char* source, int flags)
{
	(void)source; // should always be nullptr for ramfs

	struct vfs_superblock* sb = kzalloc(sizeof(struct vfs_superblock));
	if (!sb) {
		log_error("Failed to allocate superblock");
		return nullptr;
	}

	struct ramfs_sb_info* info = kzalloc(sizeof(struct ramfs_sb_info));
	if (!info) {
		log_error("Failed to allocate superblock info");
		goto clean_sb;
	}

	info->next_inode_id = 1;
	info->flags = flags;
	hash_init(info->ht);

	sb->fs_data = info;

	// The root dentry of this new ramfs instance is always named "/",
	// regardless of where it's being mounted in the larger VFS tree.
	// The 'source' argument is just a label for this instance.
	struct vfs_dentry* root_dentry = dentry_alloc(nullptr, "/");
	if (!root_dentry) {
		log_error("Failed to allocate root dentry");
		goto clean_info;
	}

	root_dentry->flags = DENTRY_DIR | DENTRY_ROOT;

	struct ramfs_dentry* rdent = kzalloc(sizeof(struct ramfs_dentry));
	if (!rdent) {
		log_error("Failed to allocate root ramfs dentry");
		goto clean_dentry;
	}
	memcpy(rdent->name, root_dentry->name, RAMFS_MAX_NAME);
	list_init(&rdent->children);
	list_init(&rdent->siblings);

	root_dentry->fs_data = rdent;

	root_dentry->inode = get_root_inode(sb);
	if (!root_dentry->inode) {
		log_error("Failed to allocate root inode");
		goto clean_rdent;
	}

	dentry_add(root_dentry);

	sb->root_dentry = root_dentry;
	sb->sops = &ramfs_sb_ops;
	info->root = rdent;

	return sb;

clean_rdent:
	kfree(rdent);
clean_dentry:
	dentry_dealloc(root_dentry);
clean_info:
	kfree(info);
clean_sb:
	kfree(sb);
	return nullptr;
}

/**
 * @brief Create a new directory within a parent directory in ramfs.
 *
 * This function creates a new directory entry and inode for a subdirectory,
 * linking it into the parent directory and initializing permissions.
 *
 * @param dir
 *   The inode representing the parent directory in which the new directory
 *   should be created. This must be a directory inode and writable.
 *
 * @param dentry
 *   The dentry structure representing the new directory entry. The name field
 *   of this dentry specifies the name of the new directory. The parent field
 *   should point to the parent directory's dentry. Upon success, the function
 *   fills the inode field with the newly created inode.
 *
 * @param mode
 *   The permissions and mode bits for the new directory (e.g.,
 * read/write/execute permissions). This is typically a bitmask specifying
 * Unix-like permissions.
 *
 * @return
 *   0 on success, or a negative error code on failure.
 */
int ramfs_mkdir(struct vfs_inode* dir, struct vfs_dentry* dentry, uint16_t mode)
{
	(void)mode;

	// TODO: Make sure we put some sort of info into our hashtable
	if (!dir || !dentry || !dentry->parent ||
	    dentry->parent->inode != dir) {
		log_error("mkdir: failed to create dir '%s': %s",
			  dentry->name,
			  vfs_get_err_name(VFS_ERR_INVAL));
		return -VFS_ERR_INVAL;
	}

	if (dir->filetype != FILETYPE_DIR) {
		log_error("mkdir: failed to create dir '%s': %s",
			  dentry->name,
			  vfs_get_err_name(VFS_ERR_NOTDIR));
		return -VFS_ERR_NOTDIR;
	}

	if (strlen(dentry->name) > VFS_MAX_NAME) {
		log_error("mkdir: failed to create dir '%s': %s",
			  dentry->name,
			  vfs_get_err_name(VFS_ERR_NAMETOOLONG));
		return -VFS_ERR_NAMETOOLONG;
	}

	struct vfs_dentry* parent = dentry->parent;

	if (does_name_exist(RAMFS_DENTRY(parent), dentry->name)) {
		log_error("mkdir: failed to create dir '%s': %s",
			  dentry->name,
			  vfs_get_err_name(VFS_ERR_EXIST));
		return -VFS_ERR_EXIST;
	}

	struct vfs_inode* node =
		new_inode(dir->sb, RAMFS_SB_INFO(dir->sb)->next_inode_id++);
	if (!node) {
		log_error("failed to create dir '%s': %s",
			  dentry->name,
			  vfs_get_err_name(VFS_ERR_NOMEM));
		return -VFS_ERR_NOMEM;
	}

	struct ramfs_dentry* rdent = kzalloc(sizeof(struct ramfs_dentry));
	if (!rdent) {
		log_error("failed to create dir '%s': %s",
			  dentry->name,
			  vfs_get_err_name(VFS_ERR_NOMEM));
		// kfree(node);
		return -VFS_ERR_NOMEM;
	}

	rdent->inode_info = RAMFS_INODE_INFO(node);
	memcpy(rdent->name, dentry->name, RAMFS_MAX_NAME);
	list_init(&rdent->children);
	list_init(&rdent->siblings);

	dentry->fs_data = rdent;

	node->filetype = FILETYPE_DIR;
	// node->permissions = mode;
	node->flags = 0;
	sync_to_info(node);

	add_child_to_list(RAMFS_DENTRY(parent), RAMFS_DENTRY(dentry));
	register_child(parent, dentry);

	dentry->inode = node;
	dentry->flags = DENTRY_DIR;
	dir->nlink++;

	log_debug("mkdir: created dir '%s' in parent '%s'",
		  dentry->name,
		  parent->name);
	return VFS_OK;
}

int ramfs_open(struct vfs_inode* inode, struct vfs_file* file)
{
	file->private_data = RAMFS_FILE(inode);

	return 0;
}

int ramfs_close(struct vfs_inode* inode, struct vfs_file* file)
{
	(void)file;
	sync_to_info(inode);
	return VFS_OK;
}

ssize_t ramfs_read(struct vfs_file* file,
		   char* buffer,
		   size_t count,
		   off_t* offset)
{
	struct ramfs_file* rf = file->private_data;

	if (!rf->data || (size_t)(*offset) > rf->size) {
		log_debug("EOF");
		return 0;
	}

	size_t to_read = MIN(rf->size - (size_t)(*offset), count);
	memcpy(buffer, rf->data + *offset, to_read);

	*offset += to_read;

	return (ssize_t)to_read;
}

int ramfs_readpage(struct vfs_inode* inode, struct page* page)
{
	if (!inode || !page) {
		return -VFS_ERR_INVAL;
	}

	void* page_vaddr = (void*)PHYS_TO_HHDM(page_to_phys(page));

	// Ramfs specific file data
	struct ramfs_file* rf = RAMFS_INODE_INFO(inode)->file;
	if (!rf || !rf->data) {
		memset(page_vaddr, 0, PAGE_SIZE);
		return VFS_OK;
	}

	size_t source_offset = (size_t)page->index * PAGE_SIZE;

	log_debug("Reading page index %lu (offset %zu) from inode %zu",
		  page->index,
		  source_offset,
		  inode->id);

	if (source_offset >= rf->size) {
		// Reading past end of file, so we just write a "hole"
		memset(page_vaddr, 0, PAGE_SIZE);
		return VFS_OK;
	}

	size_t bytes_to_copy = PAGE_SIZE;
	if (source_offset + bytes_to_copy > rf->size) {
		bytes_to_copy = rf->size - source_offset;
	}

	memcpy(page_vaddr, rf->data + source_offset, bytes_to_copy);

	// Zero the remainder
	if (bytes_to_copy < PAGE_SIZE) {
		memset((void*)((uptr)page_vaddr + bytes_to_copy),
		       0,
		       PAGE_SIZE - bytes_to_copy);
	}

	return VFS_OK;
}

ssize_t ramfs_write(struct vfs_file* file,
		    const char* buffer,
		    size_t count,
		    off_t* offset)
{
	struct ramfs_file* rf = file->private_data;

	// Ensure sufficient capacity, reallocating if necessary
	if (!rf->data || rf->size + count > rf->capacity) {
		size_t old_cap = rf->capacity;

		size_t needed = (size_t)(*offset) + count;
		size_t needed_pages = CEIL_DIV(needed, PAGE_SIZE);

		char* new_data = get_free_pages(AF_KERNEL, needed_pages);
		if (!new_data) {
			return -VFS_ERR_NOMEM;
		}

		if (rf->data && rf->size > 0) {
			memcpy(new_data, rf->data, rf->size);
		}

		free_pages(rf->data, old_cap / PAGE_SIZE);
		rf->data = new_data;
		rf->capacity = needed_pages * PAGE_SIZE;
	}

	// Write data and update file position and size
	memcpy(rf->data + (*offset), buffer, count);
	rf->size = MAX(rf->size, (size_t)(*offset) + count);
	*offset += count;
	file->dentry->inode->f_size = rf->size;

	return (ssize_t)count;
}

struct vfs_dentry* ramfs_lookup(struct vfs_inode* dir_inode,
				struct vfs_dentry* child)
{
	log_debug("Looking up '%s' in dir inode %zu",
		  child->name,
		  dir_inode ? dir_inode->id : 0);
	if (!dir_inode || dir_inode->filetype != FILETYPE_DIR) {
		return nullptr;
	}

	struct vfs_dentry* parent = child->parent;
	if (dir_inode != parent->inode) {
		return nullptr;
	}

	struct ramfs_dentry* found =
		scan_dir(RAMFS_DENTRY(parent), child->name);
	if (found) {
		// We use _alloc_inode_raw here so we don't allocate a new
		// inode_info struct since we have it saved in the dentry.
		child->inode = _alloc_inode_raw(parent->inode->sb);
		child->inode->fs_data = found->inode_info;
		sync_to_inode(child->inode);
		dentry_add(child);
		log_debug("Found existing child '%s'", child->name);
		return child;
	}

	// TODO: Should always return a dentry, just negative if doesn't exist
	return nullptr;
}

int ramfs_create(struct vfs_inode* dir,
		 struct vfs_dentry* dentry,
		 uint16_t mode)
{
	struct vfs_inode* inode =
		new_inode(dir->sb, RAMFS_SB_INFO(dir->sb)->next_inode_id++);
	if (!inode) {
		return -ENOMEM;
	}

	struct ramfs_file* rfile = kzalloc(sizeof(struct ramfs_file));
	if (!rfile) {
		// TODO: Destroy inode
		// kfree(inode);
		return -ENOMEM;
	}

	inode->filetype = FILETYPE_FILE;
	inode->f_size = 0;
	inode->permissions = mode;
	inode->nlink = 1;

	sync_to_info(inode);

	struct ramfs_inode_info* info = RAMFS_INODE_INFO(inode);
	info->file = rfile;

	info_add(dir->sb, info);

	dentry->inode = inode;

	struct ramfs_dentry* rdent = kzalloc(sizeof(struct ramfs_dentry));
	if (!rdent) {
		log_error("failed to create dir '%s': %s",
			  dentry->name,
			  vfs_get_err_name(VFS_ERR_NOMEM));
		// kfree(node);
		return -ENOMEM;
	}

	rdent->inode_info = info;
	memcpy(rdent->name, dentry->name, RAMFS_MAX_NAME);
	list_init(&rdent->children);
	list_init(&rdent->siblings);

	dentry->fs_data = rdent;

	add_child_to_list(RAMFS_DENTRY(dentry->parent), RAMFS_DENTRY(dentry));
	register_child(dentry->parent, dentry);

	log_debug("Created file '%s' (inode %zu)", dentry->name, inode->id);
	log_debug(
		"fs_data: %p, rfile: %p", (void*)inode->fs_data, (void*)rfile);

	return 0;
}

/**
 * @brief Allocates a new in-memory inode for ramfs.
 */
struct vfs_inode* ramfs_alloc_inode(struct vfs_superblock* sb)
{
	(void)sb;

	struct vfs_inode* inode = _alloc_inode_raw(sb);
	if (!inode) {
		return nullptr;
	}

	struct ramfs_inode_info* rinode =
		kzalloc(sizeof(struct ramfs_inode_info));
	if (!rinode) {
		kfree(inode);
		return nullptr;
	}

	// Allocate mapping
	inode->mapping = kzalloc(sizeof(struct inode_mapping));
	if (!inode->mapping) {
		kfree(rinode);
		kfree(inode);
		return nullptr;
	}

	inode->mapping->owner = inode;
	inode->mapping->imops = &ramfs_imops;
	hash_init(inode->mapping->page_cache);

	inode->fs_data = rinode;

	return inode;
}

int ramfs_read_inode(struct vfs_inode* inode)
{
	struct ramfs_inode_info* info =
		find_private_inode(inode->sb, inode->id);
	if (!info) {
		log_error("inode %zu not found", inode->id);
		return -VFS_ERR_NOENT;
	}

	// Populate the generic VFS inode from our private, "persistent" info.
	inode->filetype = info->filetype;
	inode->f_size = info->file->size;
	inode->flags = info->flags;
	inode->permissions = info->permissions;
	inode->fs_data = info; // Link to the file/dir data

	return VFS_OK;
}

void ramfs_destroy_inode(struct vfs_inode* inode)
{
	hash_del(&inode->hash);
	// TODO: Need to rework our directory management so in the future if
	// we deallocate a dentry we can find the data again
	// struct ramfs_file* file = RAMFS_FILE(inode);
	// if (file) {
	// 	kfree(file->data);
	// 	kfree(file);
	// }
	// kfree(inode->fs_data);
	kfree(inode);
}

int ramfs_readdir(struct vfs_file* file, struct dirent* dirent, off_t offset)
{
	if (!file || !dirent || offset < 0) {
		return -VFS_ERR_INVAL;
	}

	struct vfs_dentry* pdentry = file->dentry;

	off_t current_off = 0;
	struct vfs_dentry* child;
	list_for_each_entry (child, &pdentry->children, siblings) {
		if (child->inode == nullptr) {
			continue;
		}

		if (current_off++ < offset) {
			continue;
		}

		__fill_dirent(child, dirent);
		dirent->d_off = current_off + 1;

		return 1;
	}

	return 0;
}

void test_ramfs_readpage()
{
	log_info("--- Starting ramfs readpage test ---");

	const char* file_path = "/testfile.txt";
	char write_buffer[PAGE_SIZE * 2]; // Create 2 pages of data

	// Fill page 0 with 'A's and page 1 with 'B's
	memset(write_buffer, 'A', PAGE_SIZE);
	memset(write_buffer + PAGE_SIZE, 'B', PAGE_SIZE);

	int fd = vfs_open(file_path, O_CREAT | O_RDWR);
	if (fd < 0) {
		panic("Failed to create testfile!");
	}
	vfs_write(fd, write_buffer, sizeof(write_buffer));

	// Get file and inode
	struct vfs_file* file = get_file(fd);
	struct vfs_inode* inode = file->dentry->inode;
	log_info("Test file created with inode %p", (void*)inode);

	// Allocate destination page
	struct page* dest_page = alloc_page(AF_KERNEL);
	if (!dest_page) {
		panic("Failed to allocate destination page!");
	}
	void* dest_vaddr = (void*)PHYS_TO_HHDM(page_to_phys(dest_page));

	// Set the page's metadata to request the SECOND page of the file
	dest_page->index = 1;		     // We want the page full of 'B's
	dest_page->mapping = inode->mapping; // Point it to the correct mapping

	log_info("Calling readpage for file index %lu...", dest_page->index);

	int result = inode->mapping->imops->readpage(inode, dest_page);
	if (result < 0) {
		panic("readpage_test: readpage returned an error!");
	}

	// Verify the contents
	bool success = true;
	for (size_t i = 0; i < PAGE_SIZE; i++) {
		if (((char*)dest_vaddr)[i] != 'B') {
			log_error(
				"Verification failed at byte %zu! Expected 'B', got '%c'",
				i,
				((char*)dest_vaddr)[i]);
			success = false;
			break;
		}
	}

	if (success) {
		log_info("SUCCESS: Page contents verified correctly!");
	} else {
		panic("readpage_test: Page contents were incorrect!");
	}

	// Clean up
	free_page(dest_vaddr);
	vfs_close(fd);

	log_info("--- ramfs readpage test finished ---");
}

void ramfs_test()
{
	log_info(TESTING_HEADER, "Ramfs");

	test_ramfs_readpage();

	log_info(TESTING_FOOTER, "Ramfs");
}

/*******************************************************************************
 * Private Function Definitions
 *******************************************************************************/

static struct vfs_dentry* find_child(struct vfs_dentry* parent,
				     const char* name)
{
	struct vfs_dentry* child = nullptr;
	list_for_each_entry (child, &parent->children, siblings) {
		if (!strcmp(child->name, name)) {
			return child;
		}
	}

	return child;
}

static struct vfs_inode* get_root_inode(struct vfs_superblock* sb)
{
	if (!sb) return nullptr;

	struct vfs_inode* r_node = ramfs_alloc_inode(sb);
	if (!r_node) {
		log_error("Failed to allocate root inode");
		return nullptr;
	}

	struct ramfs_inode_info* r_info =
		kzalloc(sizeof(struct ramfs_inode_info));
	if (!r_info) {
		log_error("Failed to allocate root inode info");
		kfree(r_node);
		return nullptr;
	}

	r_node->sb = sb;
	r_node->id = 0;
	r_node->ref_count = 1;

	r_node->filetype = FILETYPE_DIR;
	r_node->permissions =
		VFS_PERM_ALL; // TODO: use stricter perms once supported.
	r_node->flags = 0;
	r_node->fs_data = r_info;

	sync_to_info(r_node);

	// Add it to the cache so future lookups will find it.
	inode_add(r_node);
	info_add(sb, r_info);

	return r_node;
}

// Adds child to parent's children list
static int add_child_to_list(struct ramfs_dentry* parent,
			     struct ramfs_dentry* child)
{
	if (!parent || !child) {
		return -VFS_ERR_INVAL;
	}

	list_add_tail(&parent->children, &child->siblings);

	return 0;
}

static struct ramfs_dentry* scan_dir(struct ramfs_dentry* dir, const char* name)
{
	struct ramfs_dentry* child;
	list_for_each_entry (child, &dir->children, siblings) {
		if (!strcmp(child->name, name)) {
			return child;
		}
	}
	return nullptr;
}

static bool does_name_exist(struct ramfs_dentry* dir, const char* name)
{
	return scan_dir(dir, name) != nullptr;
}

static struct ramfs_inode_info* find_private_inode(struct vfs_superblock* sb,
						   size_t id)
{
	struct ramfs_inode_info* candidate;
	hash_for_each_possible (RAMFS_SB_INFO(sb)->ht, candidate, hash, id) {
		log_debug("Checking candidate inode %zu", candidate->id);
		if (candidate->id == id) {
			return candidate;
		}
	}

	return nullptr;
}

static void info_add(struct vfs_superblock* sb, struct ramfs_inode_info* info)
{
	struct ramfs_sb_info* sb_info = RAMFS_SB_INFO(sb);
	struct hlist_head* bucket =
		&sb_info->ht[hash_min(info->id, RAMFS_HASH_BITS)];
	info->bucket = bucket;
	// hash_add(i_ht, &info->hash, info->id);
	hlist_add_head(bucket, &info->hash);
}

static void sync_to_inode(struct vfs_inode* inode)
{
	struct ramfs_inode_info* info = RAMFS_INODE_INFO(inode);

	inode->id = info->id;
	inode->permissions = info->permissions;
	inode->flags = info->flags;
	inode->filetype = info->filetype;
	inode->f_size = info->f_size;
}

static void sync_to_info(struct vfs_inode* inode)
{
	struct ramfs_inode_info* info = RAMFS_INODE_INFO(inode);

	info->id = inode->id;
	info->permissions = inode->permissions;
	info->flags = inode->flags;
	info->filetype = inode->filetype;
	info->f_size = inode->f_size;
}

static struct vfs_inode* _alloc_inode_raw(struct vfs_superblock* sb)
{
	(void)sb;

	struct vfs_inode* inode = kzalloc(sizeof(struct vfs_inode));
	if (!inode) {
		log_error("Failed to allocate raw inode");
		return nullptr;
	}

	inode->ops = &ramfs_ops;
	inode->fops = &ramfs_fops;

	sem_init(&inode->lock, 1);

	return inode;
}
