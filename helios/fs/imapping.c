#include <uapi/helios/errno.h>

#include "fs/imapping.h"
#include "fs/vfs.h"
#include "kernel/spinlock.h"
#include "lib/log.h"
#include "mm/page.h"

/**
 * returns locked page, also expects already locked mapping
 */
struct page* __imap_lookup(struct inode_mapping* mapping, pgoff_t index)
{
	struct page* page = nullptr;
	hash_for_each_possible (mapping->page_cache, page, map_node, index) {
		if (page->index == index) {
			lock_page(page);
			return page;
		}
	}
	return nullptr;
}

/**
 * returns locked page
 */
struct page* imap_lookup(struct inode_mapping* mapping, pgoff_t index)
{
	if (!mapping) {
		return nullptr;
	}

	struct page* page;
	scoped_spin_guard(&mapping->lock)
	{

		page = __imap_lookup(mapping, index);
	}

	return get_page(page);
}

struct page* imap_lookup_or_create(struct inode_mapping* mapping, pgoff_t index)
{
	if (!mapping) {
		return nullptr;
	}

	struct page* page;
	scoped_spin_guard(&mapping->lock)
	{
		page = __imap_lookup(mapping, index);
		if (page) {
			get_page(page);
			return page;
		}
	}

	// Since alloc_page might sleep, we have to drop the lock
	page = alloc_page(AF_KERNEL);

	spin_guard(&mapping->lock);
	struct page* temp = __imap_lookup(mapping, index);
	if (temp) {
		if (!page) {
			return page;
		}
		// TODO: Fix this shitty page_alloc API
		free_page((void*)PHYS_TO_HHDM(page_to_phys(page)));
		page = temp;
		get_page(page);
		return page;
	}

	if (!page) {
		return page;
	}

	lock_page(page);

	page->index = index;
	page->mapping = mapping;

	// Be cause we have a new page, clear uptodate
	page->flags &= ~PG_UPTODATE;
	page->flags &= ~PG_DIRTY;

	// Hash gets ref to page, this is different from the lookup ref we
	// pass to the caller
	get_page(page);
	page->flags |= PG_MAPPED;
	hash_add(mapping->page_cache, &page->map_node, page->index);

	// This the lookup ref we return to the caller
	get_page(page);

	return page;
}

/**
 * Expects locked page
 */
int imap_insert(struct inode_mapping* mapping, struct page* page)
{
	if (!mapping) {
		return -EINVAL;
	}

	spin_guard(&mapping->lock);

	get_page(page);
	page->flags |= PG_MAPPED;
	hash_add(mapping->page_cache, &page->map_node, page->index);

	return 0;
}

void imap_remove(struct inode_mapping* mapping, struct page* page)
{
	if (!mapping || !page || !(page->flags & PG_MAPPED)) {
		return;
	}

	log_debug("Removing page index %lu from mapping (ino: %zu)", page->index, mapping->owner->id);

	spin_guard(&mapping->lock);

	hash_del(&page->map_node);
	page->flags &= ~PG_MAPPED;

	put_page(page);
}
