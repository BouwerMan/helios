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

static struct slab_cache mem_cache = { 0 };

/**
 * __free_addr_space - Frees all memory regions within an address space.
 * @vas: The address space to free.
 *
 * Iterates through all memory_region structs in the address space,
 * removes them from the list, and frees them.
 */
static void __free_addr_space(struct address_space* vas);

static void inc_refcounts(struct memory_region* mr);

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

void destroy_mem_region(struct memory_region* mr)
{
	slab_free(&mem_cache, mr);
}

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

void add_region(struct address_space* vas, struct memory_region* mr)
{
	mr->owner = vas;
	list_add(&vas->mr_list, &mr->list);
}

void remove_region(struct memory_region* mr)
{
	list_del(&mr->list);
}

void vas_set_pml4(struct address_space* vas, pgd_t* pml4)
{
	if (!vas) {
		panic("Cannot set PML4 for a null address space");
	}
	vas->pml4 = pml4;
	vas->pml4_phys = HHDM_TO_PHYS((uptr)pml4);
}

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

void unmap_region(struct address_space* vas, struct memory_region* mr)
{
	if (!vas || !mr) return;

	vmm_unmap_region(vas, mr);
	remove_region(mr);
	destroy_mem_region(mr);
}

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

static void inc_refcounts(struct memory_region* mr)
{
	paddr_t start = get_phys_addr(mr->owner->pml4, mr->start);
	size_t num_pages = CEIL_DIV(mr->end - mr->start, PAGE_SIZE);

	log_debug("Start: %lx, num_pages: %zu", start, num_pages);

	for (size_t i = 0; i < num_pages; i++) {
		struct page* page = phys_to_page(start + i * PAGE_SIZE);
		get_page(page);
	}
}
