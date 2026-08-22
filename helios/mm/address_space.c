#include "fs/vfs.h"
#include "kernel/semaphores.h"
#include "kernel/spinlock.h"
#include "mm/kmalloc.h"
#include "mm/page.h"
#include "uapi/helios/mman.h"
#include <arch/mmu/vmm.h>
#include <kernel/panic.h>
#include <lib/list.h>
#include <mm/address_space.h>
#include <mm/slab.h>
#include <uapi/helios/errno.h>

/**
 * @addtogroup mm
 * @{
 */

static struct slab_cache mem_cache = { 0 };

static void __free_addr_space(struct address_space* vas);

/**
 * @brief Initializes the VMA slab cache state.
 *
 * Creates the slab cache for struct memory_region. This function is
 * idempotent and may sleep. Call it during early boot or memory-management
 * initialization.
 */
void address_space_init()
{
	if (*mem_cache.name) {
		return;
	}
	int res = slab_cache_init(&mem_cache, "Memory Regions", sizeof(struct memory_region), 0, nullptr, nullptr);
	if (res < 0) {
		panic("Could not init memory region slab cache");
	}
	log_debug("Initialized address space cache");
}

/**
 * @brief Allocates and initializes an address space.
 *
 * @return A new address_space, or NULL on out-of-memory.
 *
 * This function may sleep. It initializes the region list and both locks.
 * The caller must set the pml4 fields.
 */
struct address_space* alloc_address_space()
{
	struct address_space* vas = kzalloc(sizeof(struct address_space));
	if (!vas) {
		log_error("OOM error from kzmalloc");
		return nullptr;
	}
	list_init(&vas->mr_list);
	rwsem_init(&vas->vma_lock);
	spin_init(&vas->pgt_lock);
	return vas;
}

/**
 * @brief Allocates a memory_region descriptor.
 *
 * @param start Inclusive start virtual address. Must be page-aligned.
 * @param end Exclusive end virtual address. Must be page-aligned and
 * greater than start.
 * @param prot PROT_* permission mask.
 * @param flags MAP_* mask.
 *
 * @return A new memory_region, or NULL on out-of-memory.
 *
 * This function may sleep. It does not insert the region into any list.
 * The caller must call add_region().
 */
struct memory_region* alloc_mem_region(uptr start, uptr end, unsigned long prot, unsigned long flags)
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
 * @brief Frees a memory_region descriptor.
 *
 * @param mr Region to release.
 *
 * @note May sleep.
 */
void destroy_mem_region(struct memory_region* mr)
{
	slab_free(&mem_cache, mr);
}

/**
 * @brief Duplicates regions and sets up child mappings.
 *
 * @param dest Destination address space.
 * @param src Source address space.
 *
 * @return 0 on success, or a negative errno value on failure.
 *
 * This function may sleep. The caller must ensure dest is not yet visible
 * to any other thread. It holds src->vma_lock for reading across the whole
 * walk, including the vmm_fork_region() calls, since that function assumes
 * the caller already holds it.
 */
int address_space_dup(struct address_space* dest, struct address_space* src)
{
	log_debug("Duplicating address space");
	struct memory_region* pos = nullptr;

	down_read(&src->vma_lock);
	list_for_each_entry (pos, &src->mr_list, list) {
		struct memory_region* new_mr = alloc_mem_region(pos->start, pos->end, pos->prot, pos->flags);

		if (!new_mr) {
			log_error("Could not allocate mem region");
			up_read(&src->vma_lock);
			__free_addr_space(dest);
			return -ENOMEM;
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

		down_write(&dest->vma_lock);
		add_region(dest, new_mr);
		up_write(&dest->vma_lock);

		vmm_fork_region(dest, pos);
	}
	up_read(&src->vma_lock);

	return 0;
}

/**
 * @brief Validates VMA permissions for an address.
 *
 * @param vas Address space.
 * @param vaddr Address to check.
 * @param need_read Require read permission.
 * @param need_write Require write permission.
 * @param need_exec Require exec permission.
 *
 * @return 0 on success, or -EFAULT, -EACCES, or -EINVAL on failure.
 *
 * This function may sleep and acquires vas->vma_lock for reading.
 */
int check_access(struct address_space* vas, vaddr_t vaddr, bool need_read, bool need_write, bool need_exec)
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

	const char* kind = (mr->kind == MR_FILE) ? "FILE" : (mr->kind == MR_ANON) ? "ANON" : "DEVICE";
	char prot_str[4] = { (mr->prot & PROT_READ) ? 'r' : '-',
			     (mr->prot & PROT_WRITE) ? 'w' : '-',
			     (mr->prot & PROT_EXEC) ? 'x' : '-',
			     '\0' };
	log_debug("VMA: [%016lx..%016lx) kind=%s prot=%s flags=0x%lx private=%d",
		  (unsigned long)mr->start,
		  (unsigned long)mr->end,
		  kind,
		  prot_str,
		  (unsigned long)mr->flags,
		  (int)mr->is_private);

	if (need_exec && !(mr->prot & PROT_EXEC)) {
		log_error("NX violation at vaddr=0x%lx in %s VMA", (unsigned long)vaddr, kind);
		err = -EACCES;
		goto out;
	}
	if (need_write && !(mr->prot & PROT_WRITE)) {
		log_error("Write disallowed at vaddr=0x%lx in %s VMA", (unsigned long)vaddr, kind);
		err = -EACCES;
		goto out;
	}
	if (need_read && !(mr->prot & PROT_READ)) {
		log_error("Read disallowed at vaddr=0x%lx in %s VMA", (unsigned long)vaddr, kind);
		err = -EACCES;
		goto out;
	}

out:
	up_read(&vas->vma_lock);
	return err;
}

/**
 * @brief Inserts a region into an address space.
 *
 * @param vas Address space owner.
 * @param mr Region to insert.
 *
 * Sets mr->owner and links the region onto the head of vas->mr_list.
 *
 * @note The caller must hold vas->vma_lock for writing.
 */
void add_region(struct address_space* vas, struct memory_region* mr)
{
	mr->owner = vas;
	list_add(&vas->mr_list, &mr->list);
}

/**
 * @brief Unlinks a region from its address space.
 *
 * @param mr Region to unlink.
 *
 * @note The caller must hold mr->owner->vma_lock for writing.
 */
void remove_region(struct memory_region* mr)
{
	list_del(&mr->list);
}

/**
 * @brief Sets the top-level page table for an address space.
 *
 * @param vas Address space. Must not be NULL.
 * @param pml4 Kernel-virtual pointer to the PML4.
 *
 * Also records the physical address of the PML4.
 *
 * @note IRQ-safe. The caller ensures pml4 is valid and aligned.
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
 * @brief Creates and adds a new region descriptor.
 *
 * @param vas Address space.
 * @param file File mapping info. Used if MAP_ANONYMOUS is not set.
 * @param start Inclusive start virtual address. Must be page-aligned.
 * @param end Exclusive end virtual address. Must be page-aligned and
 * greater than start.
 * @param prot PROT_* permission mask.
 * @param flags MAP_* mask. Exactly one of MAP_PRIVATE or MAP_SHARED.
 *
 * @return 0 on success, or a negative errno value on failure.
 *
 * This function may sleep. It only creates metadata and does not populate
 * page tables.
 */
int map_region(struct address_space* vas,
	       struct mr_file file,
	       uptr start,
	       uptr end,
	       unsigned long prot,
	       unsigned long flags)
{
	log_debug("Mapping region: %lx - %lx, prot: %lx, flags: %lx", start, end, prot, flags);

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
		if (!is_page_aligned((uptr)file.file_lo) || file.file_hi < file.file_lo) {
			destroy_mem_region(mr);
			return -EINVAL;
		}
		mr->kind = MR_FILE;
		mr->file = file;
	}

	down_write(&vas->vma_lock);
	add_region(vas, mr);
	up_write(&vas->vma_lock);

	return 0;
}

/**
 * @brief Creates, adds, and eagerly maps an MR_DEVICE region.
 *
 * @param vas Address space.
 * @param start Inclusive start virtual address. Must be page-aligned.
 * @param end Exclusive end virtual address. Must be page-aligned and
 * greater than start.
 * @param paddr Physical base address to alias.
 * @param prot PROT_* permission mask.
 * @param flags MAP_* mask. MAP_SHARED is required; MAP_PRIVATE is rejected.
 *
 * @return 0 on success, or a negative errno value on failure.
 *
 * This function may sleep. Unlike map_region(), it populates page tables
 * immediately, because device regions are never demand-paged. On failure,
 * it unlinks and frees the region before it returns.
 */
int map_device_region(struct address_space* vas,
		      uptr start,
		      uptr end,
		      paddr_t paddr,
		      unsigned long prot,
		      unsigned long flags)
{
	log_debug("Mapping region: %lx - %lx, prot: %lx, flags: %lx", start, end, prot, flags);

	if (!is_page_aligned(start) || !is_page_aligned(end) || start >= end) {
		return -EINVAL;
	}

	bool want_priv = !!(flags & MAP_PRIVATE);
	if (want_priv) {
		log_debug("Devices cannot be mapped as private");
		return -EINVAL;
	}

	bool want_shared = !!(flags & MAP_SHARED);
	if (!want_shared) {
		log_debug("Devices must be shared");
		return -EINVAL;
	}

	bool want_exec = !!(prot & PROT_EXEC);
	if (want_exec) {
		log_debug("Devices are not executable");
		return -EINVAL;
	}

	struct memory_region* mr = alloc_mem_region(start, end, prot, flags);
	if (!mr) {
		return -ENOMEM;
	}

	mr->kind = MR_DEVICE;
	mr->dev.paddr = paddr;

	down_write(&vas->vma_lock);
	add_region(vas, mr);
	up_write(&vas->vma_lock);

	int err = vmm_map_device_region(vas, mr);
	if (err < 0) {
		down_write(&vas->vma_lock);
		remove_region(mr);
		up_write(&vas->vma_lock);
		destroy_mem_region(mr);
		return err;
	}

	return 0;
}

/**
 * @brief Removes mappings and drops a region.
 *
 * @param vas Address space.
 * @param mr Region to remove.
 *
 * This function may sleep. It unlinks the region before it tears down its
 * page tables, so a concurrent fault cannot find the region through
 * get_region() while its mappings are being removed.
 */
void unmap_region(struct address_space* vas, struct memory_region* mr)
{
	if (!vas || !mr) return;

	down_write(&vas->vma_lock);
	remove_region(mr);
	up_write(&vas->vma_lock);

	vmm_unmap_region(vas, mr);
	destroy_mem_region(mr);
}

/**
 * @brief Tears down all regions of an address space.
 *
 * @param vas Address space to destroy.
 *
 * This function may sleep. The caller must ensure vas is not in use
 * elsewhere. It does not free vas itself.
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
 * @brief Finds the VMA that covers an address.
 *
 * @param vas Address space.
 * @param vaddr Address to search for.
 *
 * @return The covering memory_region, or NULL if none covers the address.
 *
 * @note The caller must hold vas->vma_lock for reading or writing.
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
 * @brief Frees all region descriptors without unmapping.
 *
 * @param vas Address space.
 *
 * This function may sleep. It is an internal helper. The caller ensures no
 * concurrent users exist.
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
/** @} */
