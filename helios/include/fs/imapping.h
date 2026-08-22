/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stddef.h>

#include "kernel/spinlock.h"
#include "lib/hashtable.h"
#include "mm/page.h"

/**
 * @addtogroup fs
 * @{
 */

static constexpr size_t INODE_MAPPING_PG_CACHE_BITS = 8;

/**
 * @brief An inode's page cache: the pages that hold its file data in
 * memory.
 */
struct inode_mapping {
	struct vfs_inode* owner;	 /**< The inode this page cache belongs to. */
	struct inode_mapping_ops* imops; /**< Filesystem callbacks for reading and writing pages. */
	spinlock_t lock;		 /**< Locks page_cache. */
	DECLARE_HASHTABLE(page_cache, INODE_MAPPING_PG_CACHE_BITS); /**< Cached pages, keyed by page index. */
};

/**
 * @brief Filesystem callbacks for reading and writing an inode's pages.
 */
struct inode_mapping_ops {
	int (*readpage)(struct vfs_inode* inode, struct page* page);  /**< Fills page with data from inode. */
	int (*writepage)(struct vfs_inode* inode, struct page* page); /**< Writes page back to inode. */
};

/**
 * @brief Finds the cached page at the given index, or allocates and
 * inserts a new one.
 *
 * @param mapping The page cache to search.
 * @param index Page index within the file.
 *
 * @return The page, locked, with a reference held for the caller. Or
 * nullptr on allocation failure.
 *
 * @relates inode_mapping
 */
struct page* imap_lookup_or_create(struct inode_mapping* mapping, pgoff_t index);

/**
 * @brief Inserts an already-locked page into the page cache.
 *
 * @param mapping The page cache to insert into.
 * @param page The page to insert. Must already be locked.
 *
 * @return 0 on success, or -EINVAL if mapping is nullptr.
 *
 * @relates inode_mapping
 */
int imap_insert(struct inode_mapping* mapping, struct page* page);

/**
 * @brief Removes a page from the page cache.
 *
 * @param mapping The page cache to remove from.
 * @param page The page to remove.
 *
 * @relates inode_mapping
 */
void imap_remove(struct inode_mapping* mapping, struct page* page);

/** @} */
