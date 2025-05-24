/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stddef.h>

#include <kernel/helios.h>

#include <util/list.h>

// TODO: Try using a free list stored in the freed object's slab slice similar to the linux kernel

#define MAX_CACHE_NAME_LEN 32
// NOTE: SLAB_SIZE_PAGES can only be 1, if it is anything more than 1 address masking
// starts to break. In the future if I want to support larger page numbers I will have to
// switch my vmm and pmm allocators to a buddy allocator system and ensure size alignment.
// (align to 16 pages instead of 1 page)
#define SLAB_SIZE_PAGES 1
_Static_assert(SLAB_SIZE_PAGES == 1, "SLAB_SIZE_PAGES MUST BE 1. SEE NOTE IN " __FILE__ " FOR MORE INFO");

enum slab_cache_flags {
	CACHE_UNINITIALIZED = 0,
	CACHE_INITIALIZED,
};

// TODO: locks and shit
struct slab_cache {
	size_t object_size;
	size_t object_align;
	size_t slab_size_pages;
	size_t objects_per_slab;
	size_t header_size;
	enum slab_cache_flags flags;
	struct list empty;
	struct list partial;
	struct list full;
	struct list cache_node;
	void (*constructor)(void*);
	void (*destructor)(void*);
	char name[MAX_CACHE_NAME_LEN];
};

struct slab {
	struct list link; // points to next slab
	size_t free_top;
	struct slab_cache* parent;
	void** free_stack;
};

[[nodiscard]] int slab_cache_init(struct slab_cache* cache, const char* name, size_t object_size, size_t object_align,
				  void (*constructor)(void*), void (*destructor)(void*));
void* slab_alloc(struct slab_cache* cache);
void slab_free(struct slab_cache* cache, void* object);
void slab_cache_destroy(struct slab_cache* cache);
