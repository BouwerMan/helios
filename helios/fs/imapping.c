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

	unsigned long flags;
	spin_lock_irqsave(&mapping->lock, &flags);

	struct page* page = __imap_lookup(mapping, index);

	spin_unlock_irqrestore(&mapping->lock, flags);
	return get_page(page);
}

struct page* imap_lookup_or_create(struct inode_mapping* mapping, pgoff_t index)
{
	if (!mapping) {
		return nullptr;
	}

	unsigned long flags;
	spin_lock_irqsave(&mapping->lock, &flags);

	struct page* page = __imap_lookup(mapping, index);
	if (page) {
		get_page(page);
		goto ret_page;
	}

	spin_unlock_irqrestore(&mapping->lock, flags);

	// Since alloc_page might sleep, we have to drop the lock
	page = alloc_page(AF_KERNEL);

	spin_lock_irqsave(&mapping->lock, &flags);
	struct page* temp = __imap_lookup(mapping, index);
	if (temp) {
		if (!page) {
			goto ret_page;
		}
		// TODO: Fix this shitty page_alloc API
		free_page((void*)PHYS_TO_HHDM(page_to_phys(page)));
		page = temp;
		get_page(page);
		goto ret_page;
	}

	if (!page) {
		goto ret_page;
	}

	lock_page(page);

	page->index = index;
	page->mapping = mapping;

	// Be cause we have a new page, clear uptodate
	page->flags &= ~PG_UPTODATE;
	page->flags &= ~PG_DIRTY;

	get_page(page);
	page->flags |= PG_MAPPED;
	hash_add(mapping->page_cache, &page->map_node, page->index);

ret_page:
	spin_unlock_irqrestore(&mapping->lock, flags);
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

	unsigned long flags;
	spin_lock_irqsave(&mapping->lock, &flags);

	get_page(page);
	page->flags |= PG_MAPPED;
	hash_add(mapping->page_cache, &page->map_node, page->index);

	spin_unlock_irqrestore(&mapping->lock, flags);
	return 0;
}

void imap_remove(struct inode_mapping* mapping, struct page* page)
{
	if (!mapping || !page || !(page->flags & PG_MAPPED)) {
		return;
	}

	log_debug("Removing page index %lu from mapping (ino: %zu)",
		  page->index,
		  mapping->owner->id);

	unsigned long flags;
	spin_lock_irqsave(&mapping->lock, &flags);

	hash_del(&page->map_node);
	put_page(page);

	spin_unlock_irqrestore(&mapping->lock, flags);
}
