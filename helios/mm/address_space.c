#include <arch/mmu/vmm.h>
#include <kernel/errno.h>
#include <kernel/panic.h>
#include <mm/address_space.h>
#include <mm/slab.h>
#include <util/list.h>

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

struct memory_region*
alloc_mem_region(uptr start, uptr end, unsigned long prot, unsigned long flags)
{
	struct memory_region* mr = slab_alloc(&mem_cache);
	if (!mr) return nullptr;

	mr->start = start;
	mr->end = end;
	mr->prot = prot;
	mr->flags = flags;

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
		struct memory_region* new_mr = alloc_mem_region(
			pos->start, pos->end, pos->prot, pos->flags);

		if (!new_mr) {
			log_error("Could not allocate mem region");
			__free_addr_space(dest);
			return -1;
		}

		add_region(dest, new_mr);

		vmm_fork_region(dest, pos);

		/* The reason we are incrementing here is because
		 * we are duplicating the address space, so we need to
		 * ensure that the reference counts for the memory regions
		 * are correct in the new address space. */
		inc_refcounts(new_mr);
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
	       uptr start,
	       uptr end,
	       unsigned long prot,
	       unsigned long flags)
{
	log_debug(
		"Mapping region: start=0x%lx, end=0x%lx, prot=0x%lx, flags=0x%lx",
		start,
		end,
		prot,
		flags);
	struct memory_region* mr = alloc_mem_region(start, end, prot, flags);
	if (!mr) {
		return -ENOMEM;
	}

	int err = vmm_map_region(vas, mr);
	if (err < 0) {
		return err;
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

void address_space_dump(struct address_space* vas)
{
	if (!vas) return;

	struct memory_region* pos = NULL;
	log_info("Dumping address space");
	log_info("Start              | "
		 "End                | "
		 "Prot               | "
		 "Flags");
	log_info(
		"---------------------------------------------------------------------------------");
	list_for_each_entry (pos, &vas->mr_list, list) {
		log_info("0x%016lx | 0x%016lx | 0x%016lx | 0x%016lx",
			 pos->start,
			 pos->end,
			 pos->prot,
			 pos->flags);
	}
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
