/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stddef.h>

#include "kernel/spinlock.h"
#include "lib/hashtable.h"
#include "mm/page.h"

static constexpr size_t INODE_MAPPING_PG_CACHE_BITS = 8;

struct inode_mapping {
	struct vfs_inode* owner;
	struct inode_mapping_ops* imops;
	spinlock_t lock;
	// Put page cache here
	DECLARE_HASHTABLE(page_cache, INODE_MAPPING_PG_CACHE_BITS);
};

struct inode_mapping_ops {
	int (*readpage)(struct vfs_inode* inode, struct page* page);
	int (*writepage)(struct vfs_inode* inode, struct page* page);
};

struct page* imap_lookup_or_create(struct inode_mapping* mapping,
				   pgoff_t index);

int imap_insert(struct inode_mapping* mapping, struct page* page);
void imap_remove(struct inode_mapping* mapping, struct page* page);
