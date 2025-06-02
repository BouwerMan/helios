/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stddef.h>

#include <kernel/helios.h>

#include <util/list.h>

// TODO: Try using a free list stored in the freed object's slab slice similar to the linux kernel

#define MAX_CACHE_NAME_LEN 32

// NOTE: SLAB_SIZE_PAGES must be a power of 2.
#define SLAB_SIZE_PAGES 16
_Static_assert(IS_POWER_OF_TWO(SLAB_SIZE_PAGES) == true, "SLAB_SIZE_PAGES must be power of 2");

enum slab_cache_flags {
	CACHE_UNINITIALIZED = 0,
	CACHE_INITIALIZED,
};

// TODO: locks and shit
struct slab_cache {
	// Metadata for the slab cache
	size_t object_size;
	size_t object_align;
	size_t slab_size_pages;
	size_t objects_per_slab;
	size_t header_size;
	enum slab_cache_flags flags;

	// Free lists of slabs
	struct list empty;
	struct list partial;
	struct list full;
	struct list cache_node;

	// Object lifecycle management
	void (*constructor)(void*);
	void (*destructor)(void*);

	// Statistics
	size_t total_slabs;   // Number of active slabs
	size_t total_objects; // Total number of objects managed by all slabs
	size_t used_objects;  // Currently allocated (live) objects

	char name[MAX_CACHE_NAME_LEN];
};

struct slab {
	struct list link; // points to next slab
	size_t free_top;
	struct slab_cache* parent;
	void** free_stack;
};

[[nodiscard]]
int slab_cache_init(struct slab_cache* cache, const char* name, size_t object_size, size_t object_align,
		    void (*constructor)(void*), void (*destructor)(void*));
[[nodiscard, gnu::malloc]]
void* slab_alloc(struct slab_cache* cache);
void slab_free(struct slab_cache* cache, void* object);
void slab_cache_destroy(struct slab_cache* cache);
void slab_dump_stats(struct slab_cache* cache);
