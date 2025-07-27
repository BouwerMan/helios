#include <drivers/fs/ramfs.h>
#include <drivers/fs/vfs.h>
#include <mm/page.h>
#include <mm/page_alloc.h>
#include <mm/slab.h>
#include <stdlib.h>
#include <string.h>
#include <util/log.h>

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

};

/*******************************************************************************
* Private Function Prototypes
*******************************************************************************/

/**
 * Scans dir for name in it's child list.
 */
static struct vfs_dentry* scan_dir(struct vfs_dentry* dir, const char* name);
static struct vfs_inode* get_root_inode(struct vfs_superblock* sb);
static int add_child_to_list(struct vfs_dentry* parent, struct vfs_dentry* child);

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
	(void)flags; // Temporary until I actually support flags
	extern struct slab_cache dentry_cache;

	struct vfs_superblock* sb = kmalloc(sizeof(struct vfs_superblock));
	if (!sb) {
		return nullptr;
	}

	struct vfs_dentry* root_dentry = dentry_alloc(nullptr, source);
	if (!root_dentry) {
		goto clean_sb;
	}

	root_dentry->flags = DENTRY_DIR | DENTRY_ROOT;

	root_dentry->inode = get_root_inode(sb);
	if (!root_dentry->inode) {
		goto clean_dentry;
	}

	dentry_add(root_dentry);

	sb->root_dentry = root_dentry;

	return sb;

clean_dentry:
	dentry_dealloc(root_dentry);
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
 *   The permissions and mode bits for the new directory (e.g., read/write/execute
 *   permissions). This is typically a bitmask specifying Unix-like permissions.
 *
 * @return
 *   0 on success, or a negative error code on failure.
 */
int ramfs_mkdir(struct vfs_inode* dir, struct vfs_dentry* dentry, uint16_t mode)
{
	if (!dir || !dentry || !dentry->parent || dentry->parent->inode != dir) {
		log_error("mkdir: failed to create dir '%s': %s", dentry->name, vfs_get_err_name(VFS_ERR_INVAL));
		return -VFS_ERR_INVAL;
	}

	if (dir->filetype != FILETYPE_DIR) {
		log_error("mkdir: failed to create dir '%s': %s", dentry->name, vfs_get_err_name(VFS_ERR_NOTDIR));
		return -VFS_ERR_NOTDIR;
	}

	if (strlen(dentry->name) > VFS_MAX_NAME) {
		log_error("mkdir: failed to create dir '%s': %s", dentry->name, vfs_get_err_name(VFS_ERR_NAMETOOLONG));
		return -VFS_ERR_NAMETOOLONG;
	}

	struct vfs_dentry* parent = dentry->parent;

	if (vfs_does_name_exist(parent, dentry->name)) {
		log_error("mkdir: failed to create dir '%s': %s", dentry->name, vfs_get_err_name(VFS_ERR_EXIST));
		return -VFS_ERR_EXIST;
	}

	// TODO: per fs inode creation
	struct vfs_inode* node = kzmalloc(sizeof(struct vfs_inode));
	if (!node) {
		log_error("mkdir: failed to create dir '%s': %s", dentry->name, vfs_get_err_name(VFS_ERR_NOMEM));
		return -VFS_ERR_NOMEM;
	}

	node->id = (size_t)vfs_get_next_id();
	node->filetype = FILETYPE_DIR;
	node->ref_count = 1;
	node->permissions = mode;
	node->flags = 0;

	node->sb = parent->inode->sb;
	node->ops = &ramfs_ops;

	add_child_to_list(parent, dentry);

	dentry->inode = node;
	dentry->flags = DENTRY_DIR;
	dir->nlink++;

	log_debug("mkdir: created dir '%s' in parent '%s'", dentry->name, parent->name);
	return VFS_OK;
}

int ramfs_open(struct vfs_inode* inode, struct vfs_file* file)
{
	file->private_data = inode->fs_data;

	return VFS_OK;
}

int ramfs_close(struct vfs_inode* inode, struct vfs_file* file)
{
	(void)inode;
	(void)file;
	return VFS_OK;
}

ssize_t ramfs_read(struct vfs_file* file, char* buffer, size_t count)
{
	struct ramfs_file* rf = file->private_data;

	if (!rf->data || (size_t)file->f_pos > rf->size) {
		log_debug("EOF");
		return 0;
	}

	size_t to_read = MIN(rf->size - (size_t)file->f_pos, count);
	memcpy(buffer, rf->data + file->f_pos, to_read);

	file->f_pos += to_read;

	return (ssize_t)to_read;
}

ssize_t ramfs_write(struct vfs_file* file, const char* buffer, size_t count)
{
	struct ramfs_file* rf = file->private_data;

	// Ensure sufficient capacity, reallocating if necessary
	if (!rf->data || rf->size + count > rf->capacity) {
		size_t old_cap = rf->capacity;

		size_t needed = (size_t)file->f_pos + count;
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
	memcpy(rf->data + file->f_pos, buffer, count);
	rf->size = MAX(rf->size, (size_t)file->f_pos + count);
	file->f_pos += count;
	file->dentry->inode->f_size = rf->size;

	return (ssize_t)count;
}

struct vfs_dentry* ramfs_lookup(struct vfs_inode* dir_inode, struct vfs_dentry* child)
{
	if (!dir_inode || dir_inode->filetype != FILETYPE_DIR) {
		return nullptr;
	}

	struct vfs_dentry* parent = child->parent;
	if (dir_inode != parent->inode) {
		return nullptr;
	}

	struct vfs_dentry* found = scan_dir(parent, child->name);
	if (found) {
		child->inode = found->inode;
		dentry_add(child);
		return child;
	}

	return nullptr;
}

int ramfs_create(struct vfs_inode* dir, struct vfs_dentry* dentry, uint16_t mode)
{
	// TODO: Per fs inode allocation
	struct vfs_inode* inode = kzmalloc(sizeof(struct vfs_inode));
	if (!inode) {
		return -VFS_ERR_NOMEM;
	}

	struct ramfs_file* rfile = kzmalloc(sizeof(struct ramfs_file));
	if (!rfile) {
		kfree(inode);
		return -VFS_ERR_NOMEM;
	}

	inode->id = (size_t)vfs_get_next_id();
	inode->filetype = FILETYPE_FILE;
	inode->f_size = 0;
	inode->ref_count = 1;
	inode->permissions = mode;
	inode->flags = 0;
	inode->ops = &ramfs_ops;
	inode->fops = &ramfs_fops;
	inode->sb = dir->sb;
	inode->nlink = 1;
	inode->fs_data = rfile;

	dentry->inode = inode;

	add_child_to_list(dentry->parent, dentry);

	log_debug("Created file '%s' (inode %zu)", dentry->name, inode->id);

	return VFS_OK;
}

/*******************************************************************************
* Private Function Definitions
*******************************************************************************/

static struct vfs_inode* get_root_inode(struct vfs_superblock* sb)
{
	if (!sb) return nullptr;

	struct vfs_inode* r_node = kmalloc(sizeof(struct vfs_inode));
	if (!r_node) return nullptr;

	r_node->id = 0;
	r_node->filetype = FILETYPE_DIR;
	r_node->ref_count = 1;
	r_node->permissions = VFS_PERM_ALL; // TODO: use stricter perms once supported.
	r_node->flags = 0;

	r_node->sb = sb;
	r_node->ops = &ramfs_ops;

	return r_node;
}

// Adds child to parent's children list
static int add_child_to_list(struct vfs_dentry* parent, struct vfs_dentry* child)
{
	if (!parent || !child) {
		return -VFS_ERR_INVAL;
	}

	list_add_tail(&parent->children, &child->siblings);

	return 0;
}

static struct vfs_dentry* scan_dir(struct vfs_dentry* dir, const char* name)
{
	struct vfs_dentry* child;
	list_for_each_entry (child, &dir->children, siblings) {
		if (!strcmp(child->name, name)) {
			return child;
		}
	}
	return nullptr;
}
