#include "fs/vfs.h"
#include "mm/kmalloc.h"
#include "mm/page.h"
#include "uapi/helios/mman.h"
#include <arch/mmu/vmm.h>
#include <kernel/panic.h>
#include <lib/list.h>
#include <mm/address_space.h>
#include <mm/slab.h>
#include <uapi/helios/errno.h>

// TODO: Use locks when modifying address space

static struct slab_cache mem_cache = { 0 };

static void __free_addr_space(struct address_space* vas);

/**
 * address_space_init - Initialize VMA slab/cache state
 * Return: none
 * Context: Early boot or mm init; may sleep.
 * Notes: Idempotent. Creates slab cache for struct memory_region.
 */
void address_space_init()
{
	if (*mem_cache.name) {
		return;
	}
	int res = slab_cache_init(&mem_cache,
				  "Memory Regions",
				  sizeof(struct memory_region),
				  0,
				  nullptr,
				  nullptr);
	if (res < 0) {
		panic("Could not init memory region slab cache");
	}
	log_debug("Initialized address space cache");
}

/**
 * alloc_address_space - Allocate and initialize an address_space
 * Return: New address_space* or NULL on OOM
 * Context: May sleep.
 * Notes: Initializes region list; caller must set pml4 fields.
 */
struct address_space* alloc_address_space()
{
	struct address_space* vas = kzalloc(sizeof(struct address_space));
	if (!vas) {
		log_error("OOM error from kzmalloc");
		return nullptr;
	}
	list_init(&vas->mr_list);
	return vas;
}

/**
 * alloc_mem_region - Allocate a memory_region descriptor
 * @start: Inclusive start VA (page-aligned)
 * @end:   Exclusive end VA (page-aligned, > @start)
 * @prot:  PROT_* mask
 * @flags: MAP_* mask
 * Return: New memory_region* or NULL on OOM
 * Context: May sleep.
 * Notes: Does not insert into any list; caller must add_region().
 */
struct memory_region*
alloc_mem_region(uptr start, uptr end, unsigned long prot, unsigned long flags)
{
	struct memory_region* mr = slab_alloc(&mem_cache);
	if (!mr) return nullptr;

	mr->start = start;
	mr->end = end;
	mr->prot = prot;
	mr->flags = flags;

	list_init(&mr->list);

	return mr;
}

/**
 * destroy_mem_region - Free a memory_region descriptor
 * @mr: Region to release
 * Return: none
 * Context: May sleep.
 */
void destroy_mem_region(struct memory_region* mr)
{
	slab_free(&mem_cache, mr);
}

/**
 * address_space_dup - Duplicate regions and set up child mappings
 * @dest: Destination address space
 * @src:  Source address space
 * Return: 0 on success, -errno on failure
 * Context: May sleep. Caller must ensure @dest is empty and stable.
 * Notes: Clones region metadata, adds to @dest, then forks mappings via
 *        vmm_fork_region(). On failure, frees @dest contents.
 */
int address_space_dup(struct address_space* dest, struct address_space* src)
{
	log_debug("Duplicating address space");
	struct memory_region* pos = nullptr;
	list_for_each_entry (pos, &src->mr_list, list) {
		struct memory_region* new_mr = alloc_mem_region(pos->start,
								pos->end,
								pos->prot,
								pos->flags);

		if (!new_mr) {
			log_error("Could not allocate mem region");
			__free_addr_space(dest);
			return -1;
		}

		// new_mr->file_inode = pos->file_inode;
		// new_mr->file_offset = pos->file_offset;
		new_mr->kind = pos->kind;
		new_mr->is_private = pos->is_private;
		if (pos->flags & MAP_ANONYMOUS) {
			new_mr->kind = MR_ANON;
			new_mr->anon.tag = pos->anon.tag;
		} else if (pos->kind == MR_FILE) {
			new_mr->file = pos->file;
		}

		add_region(dest, new_mr);

		vmm_fork_region(dest, pos);
	}

	return 0;
}

/**
 * check_access - Validate VMA permissions for an address
 * @vas:        Address space
 * @vaddr:      Address to check
 * @need_read:  Require read permission
 * @need_write: Require write permission
 * @need_exec:  Require exec permission
 * Return: 0 or -EFAULT/-EACCES/-EINVAL
 * Context: May sleep. Locks: acquires @vas->vma_lock (read).
 * Notes: Logs VMA summary on hit; errors if no covering VMA or perms fail.
 */
int check_access(struct address_space* vas,
		 vaddr_t vaddr,
		 bool need_read,
		 bool need_write,
		 bool need_exec)
{
	if (!vas) {
		return -EINVAL;
	}

	int err = 0;

	down_read(&vas->vma_lock);
	struct memory_region* mr = get_region(vas, vaddr);
	if (!mr) {
		log_error("No VMA covers vaddr=0x%lx", (unsigned long)vaddr);
		err = -EFAULT;
		goto out;
	}

	const char* kind = (mr->kind == MR_FILE) ? "FILE" :
			   (mr->kind == MR_ANON) ? "ANON" :
						   "DEVICE";
	char prot_str[4] = { (mr->prot & PROT_READ) ? 'r' : '-',
			     (mr->prot & PROT_WRITE) ? 'w' : '-',
			     (mr->prot & PROT_EXEC) ? 'x' : '-',
			     '\0' };
	log_debug(
		"VMA: [%016lx..%016lx) kind=%s prot=%s flags=0x%lx private=%d",
		(unsigned long)mr->start,
		(unsigned long)mr->end,
		kind,
		prot_str,
		(unsigned long)mr->flags,
		(int)mr->is_private);

	if (need_exec && !(mr->prot & PROT_EXEC)) {
		log_error("NX violation at vaddr=0x%lx in %s VMA",
			  (unsigned long)vaddr,
			  kind);
		err = -EACCES;
		goto out;
	}
	if (need_write && !(mr->prot & PROT_WRITE)) {
		log_error("Write disallowed at vaddr=0x%lx in %s VMA",
			  (unsigned long)vaddr,
			  kind);
		err = -EACCES;
		goto out;
	}
	if (need_read && !(mr->prot & PROT_READ)) {
		log_error("Read disallowed at vaddr=0x%lx in %s VMA",
			  (unsigned long)vaddr,
			  kind);
		err = -EACCES;
		goto out;
	}

out:
	up_read(&vas->vma_lock);
	return err;
}

/**
 * add_region - Insert a region into an address space
 * @vas: Address space owner
 * @mr:  Region to insert
 * Return: none
 * Context: Caller must hold @vas->vma_lock (write).
 * Notes: Sets @mr->owner and links onto @vas->mr_list head.
 */
void add_region(struct address_space* vas, struct memory_region* mr)
{
	mr->owner = vas;
	list_add(&vas->mr_list, &mr->list);
}

/**
 * remove_region - Unlink a region from its address space
 * @mr: Region to unlink
 * Return: none
 * Context: Caller must hold @mr->owner->vma_lock (write).
 */
void remove_region(struct memory_region* mr)
{
	list_del(&mr->list);
}

/**
 * vas_set_pml4 - Set top-level page table for an address space
 * @vas:  Address space (must be non-NULL)
 * @pml4: Kernel-virtual pointer to PML4
 * Return: none
 * Context: IRQ-safe. Caller ensures @pml4 is valid and aligned.
 * Notes: Also records physical address of the PML4.
 */
void vas_set_pml4(struct address_space* vas, pgd_t* pml4)
{
	if (!vas) {
		panic("Cannot set PML4 for a null address space");
	}
	vas->pml4 = pml4;
	vas->pml4_phys = HHDM_TO_PHYS((uptr)pml4);
}

/**
 * map_region - Create and add a new region descriptor
 * @vas:   Address space
 * @file:  File mapping info (used if !MAP_ANONYMOUS)
 * @start: Inclusive start VA (page-aligned)
 * @end:   Exclusive end VA (page-aligned, > @start)
 * @prot:  PROT_* mask
 * @flags: MAP_* mask (exactly one of PRIVATE/SHARED)
 * Return: 0 on success, -errno otherwise
 * Context: May sleep. Caller must hold @vas->vma_lock (write).
 * Notes: Only creates metadata; does not populate page tables.
 */
int map_region(struct address_space* vas,
	       struct mr_file file,
	       uptr start,
	       uptr end,
	       unsigned long prot,
	       unsigned long flags)
{
	log_debug("Mapping region: %lx - %lx, prot: %lx, flags: %lx",
		  start,
		  end,
		  prot,
		  flags);

	if (!is_page_aligned(start) || !is_page_aligned(end) || start >= end) {
		return -EINVAL;
	}

	bool want_priv = !!(flags & MAP_PRIVATE);
	bool want_shared = !!(flags & MAP_SHARED);
	if (want_priv == want_shared) { // must be exactly one
		return -EINVAL;
	}

	struct memory_region* mr = alloc_mem_region(start, end, prot, flags);
	if (!mr) {
		return -ENOMEM;
	}

	mr->is_private = want_priv;

	if (flags & MAP_ANONYMOUS) {
		// Anonymous mapping, not backed by a file
		mr->kind = MR_ANON;
		mr->anon.tag = 0;
	} else {
		// File-backed mapping
		if (!file.inode) {
			destroy_mem_region(mr);
			return -EINVAL;
		}
		if (!is_page_aligned((uptr)file.file_lo) ||
		    file.file_hi < file.file_lo) {
			destroy_mem_region(mr);
			return -EINVAL;
		}
		mr->kind = MR_FILE;
		mr->file = file;
	}

	add_region(vas, mr);

	return 0;
}

/**
 * unmap_region - Remove mappings and drop a region
 * @vas: Address space
 * @mr:  Region to remove
 * Return: none
 * Context: May sleep. Caller must hold @vas->vma_lock (write).
 * Notes: Calls vmm_unmap_region(), unlinks, and frees the descriptor.
 */
void unmap_region(struct address_space* vas, struct memory_region* mr)
{
	if (!vas || !mr) return;

	vmm_unmap_region(vas, mr);
	remove_region(mr);
	destroy_mem_region(mr);
}

/**
 * address_space_destroy - Tear down all regions of an address space
 * @vas: Address space to destroy
 * Return: none
 * Context: May sleep. Caller must ensure @vas not in use elsewhere.
 * Notes: Unmaps and frees all regions; does not free @vas itself.
 */
void address_space_destroy(struct address_space* vas)
{
	if (!vas) return;

	struct memory_region* pos = nullptr;
	struct memory_region* temp = nullptr;
	list_for_each_entry_safe(pos, temp, &vas->mr_list, list)
	{
		unmap_region(vas, pos);
	}
}

/**
 * get_region - Find VMA covering an address
 * @vas:   Address space
 * @vaddr: Address to search
 * Return: Covering memory_region* or NULL if none
 * Context: Caller must hold @vas->vma_lock (read or write).
 */
struct memory_region* get_region(struct address_space* vas, vaddr_t vaddr)
{
	struct memory_region* pos = nullptr;
	list_for_each_entry (pos, &vas->mr_list, list) {
		if (is_within_region(pos, vaddr)) {
			return pos;
		}
	}

	return pos;
}

/**
 * __free_addr_space - Free all region descriptors (no unmap)
 * @vas: Address space
 * Return: none
 * Context: May sleep. Internal helper; caller ensures no concurrent users.
 * Notes: Unlinks and frees descriptors without touching page tables.
 */
static void __free_addr_space(struct address_space* vas)
{
	struct memory_region* pos = nullptr;
	struct memory_region* temp = nullptr;
	list_for_each_entry_safe(pos, temp, &vas->mr_list, list)
	{
		remove_region(pos);
		slab_free(&mem_cache, pos);
	}
}
