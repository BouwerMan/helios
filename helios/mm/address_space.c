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

int address_space_dup(struct address_space* dest, struct address_space* src)
{

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
	list_remove(&mr->list);
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
