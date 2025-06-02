/**
 * @file kernel/memory/slab.c
 *
 * Copyright (C) 2025  Dylan Parks
 *
 * This file is part of HeliOS
 *
 * HeliOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <string.h>

#include <arch/x86_64/cache.h>
#include <kernel/liballoc.h>
#include <kernel/panic.h>
#include <mm/page_alloc.h>
#include <mm/slab.h>
#include <util/log.h>

// TODO: Implmement redzone
#define POISON_PATTERN	  0x5A
#define POISON_BYTE_COUNT 16 // Number of bytes to verify at head/tail

#if SLAB_DEBUG
static void slab_assert_poison(const void* obj, size_t size)
{
	const uint8_t* byte_ptr = (const uint8_t*)obj;

	// How many bytes to check at head and tail?
	size_t check_len = POISON_BYTE_COUNT;
	if (check_len * 2 > size) {
		check_len = size / 2; // round down if not enough space
	}

	for (size_t i = 0; i < check_len; i++) {
		kassert(byte_ptr[i] == POISON_PATTERN && "Use-before-init detected at start of object");
		kassert(byte_ptr[size - 1 - i] == POISON_PATTERN && "Use-before-init detected at end of object");
	}
}
#endif

[[nodiscard]]
static int slab_grow(struct slab_cache* cache);

static void destroy_slab(struct slab* slab);

[[nodiscard, gnu::malloc, gnu::always_inline]]
static inline void* slab_alloc_pages(size_t pages)
{
	return (void*)get_free_pages(0, pages);
}

[[gnu::always_inline]]
static inline void slab_free_pages(void* addr, size_t pages)
{
	free_pages(addr, pages);
}

/**
 * @brief Initialize a slab cache for fixed‐size object allocations.
 *
 * Sets up the metadata and free‐list structures needed to carve pages into
 * fixed-size objects. Verifies that the requested alignment is a power of two,
 * rounds the object size up to meet that alignment, computes how many objects
 * fit per slab, initializes the empty/partial/full slab lists, stores the
 * cache’s name, and registers optional constructor/destructor callbacks for
 * new or recycled objects.
 *
 * @param cache         Pointer to an uninitialized slab_cache structure to set up.
 * @param name          Human-readable identifier for this cache (max length MAX_CACHE_NAME_LEN).
 * @param object_size   Desired size of each object; will be rounded up to object_align.
 * @param object_align  Alignment boundary for each object; must be power of two. Defaults to L1_CACHE_SIZE if 0 is passed through.
 * @param constructor   Optional callback invoked on each object when a new slab is populated.
 * @param destructor    Optional callback invoked on each object before it’s recycled or cache is destroyed.
 * @return              0 on success, or a negative error code on failure.
 */
[[nodiscard]]
int slab_cache_init(struct slab_cache* cache, const char* name, size_t object_size, size_t object_align,
		    void (*constructor)(void*), void (*destructor)(void*))
{
	if (!cache) {
		log_error("Must supply valid cache structure pointer");
		return -ENULLPTR;
	}

	if (object_align == 0) {
		object_align = L1_CACHE_SIZE; // Default to L1 cache line size
		log_debug("Using default object alignment: %lu", object_align);
	} else {
		// Clamp the alignment to a minimum of sizeof(void*).
		// This is because freelist traversal breaks if the alignment is smaller than a pointer size.
		object_align = MAX(object_align, sizeof(void*));
	}

	if (!IS_POWER_OF_TWO(object_align)) {
		log_error("Object alignment is not a power of 2: %lu", object_align);
		return -EALIGN;
	}

	if (object_size >= PAGE_SIZE) {
		log_error("Object size of %lu is basically the same size as a page.", object_size);
		return -EOOM; // Technically not OOM but idk what it should actually be
	}

	// Zero out cache just to make sure there is no garbage data there
	memset(cache, 0, sizeof(struct slab_cache));

	// Compute rounded object size and initialize cache metadata
	const size_t rounded_size = ALIGN_UP(object_size, object_align);
	cache->object_size = rounded_size;
	cache->object_align = object_align;
	cache->slab_size_pages = SLAB_SIZE_PAGES;
	cache->header_size = ALIGN_UP(sizeof(struct slab), object_align);
	cache->objects_per_slab = (cache->slab_size_pages * PAGE_SIZE - cache->header_size) / rounded_size;

	list_init(&cache->empty);
	list_init(&cache->partial);
	list_init(&cache->full);
	list_append(&kernel.slab_caches, &cache->cache_node);

	cache->constructor = constructor;
	cache->destructor = destructor;

	cache->total_slabs = 0;
	cache->total_objects = 0;
	cache->used_objects = 0;

	// Copy the cache name and ensure null termination
	strncpy(cache->name, name, MAX_CACHE_NAME_LEN);
	cache->name[MAX_CACHE_NAME_LEN - 1] = '\0';

	cache->flags = CACHE_INITIALIZED;

	log_debug("Slab cache initialized: name=%s, object_size=%lu, object_align=%lu, "
		  "slab_size_pages=%lu, header_size=%lu, objects_per_slab=%lu",
		  name, rounded_size, object_align, cache->slab_size_pages, cache->header_size,
		  cache->objects_per_slab);
	return 0;
}

/**
 * @brief Allocate one object from the slab cache.
 *
 * Attempts to grab a free object slot from a partially filled slab.  If no
 * partial slab is available, it will pull an empty slab (or carve a new one
 * from pages), move it into the partial list, pop the first free slot, update
 * the slab’s free‐count, and move the slab to the full list if now exhausted.
 *
 * @param cache	Pointer to the slab_cache from which to allocate.
 * @return	Pointer to the allocated object, or NULL if allocation fails.
 */
[[nodiscard, gnu::malloc]]
void* slab_alloc(struct slab_cache* cache)
{
	if (!cache || cache->flags == CACHE_UNINITIALIZED) {
		log_error("Invalid or uninitialized cache");
		return NULL;
	}

	struct slab* slab = NULL;
	int res = 0;

	// Attempt to allocate from a partially filled slab
	if (!list_empty(&cache->partial)) {
		slab = list_entry(cache->partial.next, struct slab, link);
	} else if (!list_empty(&cache->empty) || (res = slab_grow(cache)) >= 0) {
		// Move the first empty slab to the partial list
		slab = list_entry(cache->empty.next, struct slab, link);
		list_move(&slab->link, &cache->partial);
		log_debug("Cache %s: slab %p moved from empty to partial (free_top=%zu/%zu)", cache->name, (void*)slab,
			  slab->free_top, cache->objects_per_slab);
	} else {
		log_error("Could not create more slabs, slab_grow returned: %d", res);
		return NULL;
	}

	void* addr = slab->free_stack[--slab->free_top];

	// Move the slab to the full list if it becomes exhausted
	if (slab->free_top == 0) {
		list_move(&slab->link, &cache->full);
		log_debug("Cache %s: slab %p is now full (free_top=%zu/%zu)", cache->name, (void*)slab, slab->free_top,
			  cache->objects_per_slab);
	}

#if SLAB_DEBUG
	slab_assert_poison(addr, cache->object_size);
#endif

	if (cache->constructor) cache->constructor(addr);

	log_debug("Cache %s: allocated object %p from slab %p (free_top=%zu/%zu)", cache->name, addr, (void*)slab,
		  slab->free_top, cache->objects_per_slab);

	cache->used_objects++;
	return addr;
}

/**
 * @brief Return an object back to its slab cache.
 *
 * Takes an object pointer previously returned by slab_alloc, identifies its
 * parent slab, optionally invokes the destructor, pushes the slot back onto
 * the slab’s free‐list, updates the slab’s free‐count, and moves the slab
 * between the full/partial/empty lists as its occupancy changes.  May free
 * completely empty slabs back to the page allocator per policy.
 *
 * @param cache  Pointer to the slab_cache managing the object.
 * @param object Pointer to the object to free; must have originated from this cache.
 */
void slab_free(struct slab_cache* cache, void* object)
{
	if (!cache) {
		log_error("I need a cache in order to do anything :/");
		return;
	}
	if (!object) {
		log_error(
			"I mean you gave me a cache but you should also give me a object to free otherwise I might just delete everything in a blind rage");
		return;
	}
	if (cache->flags == CACHE_UNINITIALIZED) {
		log_error("Supplied uninitialized cache");
		return;
	}

	// I am currently doing address masking, this works because I am only using 1 page slabs.
	// If I use larger page sizes then I should set mask to ~(slab_bytes - 1) where
	size_t slab_bytes = cache->slab_size_pages * PAGE_SIZE;
	uint64_t mask = ~(slab_bytes - 1);
	struct slab* slab = (struct slab*)((uintptr_t)object & mask);
	if (slab->parent != cache) {
		log_error("Somehow got the wrong slab (parent doesn't match the cache), good luck debugging this one");
		return;
	}

	if (slab->free_top >= cache->objects_per_slab) {
		log_error("Free top overflow for slab %p in cache %s", (void*)slab, cache->name);
		return;
	}

	if (cache->destructor) cache->destructor(object);

	slab->free_stack[slab->free_top++] = object;

	if (slab->free_top == cache->objects_per_slab) {
		log_debug("Cache %s: slab %p is now empty (free_top=%zu/%zu)", cache->name, (void*)slab, slab->free_top,
			  cache->objects_per_slab);
		list_move(&slab->link, &cache->empty);
		// TODO: If number of empty slabs is too high, destroy some of them
	} else if (slab->free_top == 1) {
		log_debug("Cache %s: slab %p moved from full to partial (free_top=%zu/%zu)", cache->name, (void*)slab,
			  slab->free_top, cache->objects_per_slab);
		list_move(&slab->link, &cache->partial);
	}

	cache->used_objects--;

#ifdef SLAB_DEBUG
	memset(object, POISON_PATTERN, cache->object_size); // Fill with a pattern for debugging
#endif

	log_debug("Cache %s: freed object %p to slab %p (free_top=%zu/%zu)", cache->name, object, (void*)slab,
		  slab->free_top, cache->objects_per_slab);
}

/**
 * @brief Destroy a slab cache and release all its memory.
 *
 * Walks all slabs in empty, partial, and full lists, invoking the destructor
 * on any remaining live objects, returning each slab’s pages back to the
 * underlying physical memory allocator, and clearing the cache’s metadata so
 * it can be safely reinitialized or discarded.
 *
 * @param cache Pointer to the slab_cache to destroy.
 */
void slab_cache_destroy(struct slab_cache* cache)
{
	if (!cache) {
		log_error("I can't destroy a cache if you don't give me a valid cache");
		return;
	}
	if (cache->flags == CACHE_UNINITIALIZED) {
		log_error("Supplied uninitialized cache");
		return;
	}

	log_debug("Destroying cache %s", cache->name);

	struct slab* slab;
	// Full slabs
	while (!list_empty(&cache->full)) {
		slab = list_entry(cache->full.next, struct slab, link);
		destroy_slab(slab);
	}
	// Partial slabs
	while (!list_empty(&cache->partial)) {
		slab = list_entry(cache->partial.next, struct slab, link);
		destroy_slab(slab);
	}
	// Empty slabs
	while (!list_empty(&cache->empty)) {
		slab = list_entry(cache->empty.next, struct slab, link);
		destroy_slab(slab);
	}

	list_remove(&cache->cache_node);

	// This also sets the flags to CACHE_UNINITIALIZED
	memset(cache, 0, sizeof(struct slab_cache));
}

/**
 * @brief Destroy a single slab and release its memory.
 *
 * Frees all objects in the slab, invoking the destructor for each live object.
 * Releases the memory allocated for the slab and its metadata.
 *
 * @param slab Pointer to the slab to destroy.
 */
static void destroy_slab(struct slab* slab)
{
	struct slab_cache* cache = slab->parent;
	log_debug("Cache %s: Destroying slab %p", cache->name, (void*)slab);

	void* base = (void*)slab;
	size_t N = cache->objects_per_slab;

	if (slab->free_top == 0) {
		// Full slab, destructor called on all objects
		for (size_t i = 0; i < N; i++) {
			void* obj = (void*)((uintptr_t)base + cache->header_size + i * cache->object_size);
			if (cache->destructor) cache->destructor(obj);
		}
	} else if (slab->free_top == N) {
		// empty slab: no live objects
		;
	} else {
		// Partial list, we have to do some funky shit
		// Temporary bitmap, too lazy to make a proper one so bools will do
		bool* is_free = kmalloc(N * sizeof(bool));
		memset(is_free, 0, N * sizeof(bool));
		// mark every free object
		for (size_t i = 0; i < slab->free_top; i++) {
			uintptr_t ptr = (uintptr_t)slab->free_stack[i];
			size_t idx = (ptr - (uintptr_t)base - cache->header_size) / cache->object_size;
			is_free[idx] = true;
		}
		// Now call destructor on each non free item
		for (size_t i = 0; i < N; i++) {
			if (!is_free[i]) {
				void* obj = (void*)((uintptr_t)base + cache->header_size + i * cache->object_size);
				if (cache->destructor) cache->destructor(obj);
			}
		}
		kfree(is_free);
	}

	list_remove(&slab->link);
	kfree(slab->free_stack);
	slab_free_pages(base, cache->slab_size_pages);

	cache->total_slabs--;
	cache->total_objects -= cache->objects_per_slab;
}

/**
 * @brief Grow the slab cache by allocating a new slab.
 *
 * Allocates memory for a new slab, initializes its metadata, and adds it to
 * the empty slab list of the cache.
 *
 * @param cache Pointer to the slab_cache to grow.
 * @return 0 on success, -EOOM if out of memory.
 */
[[nodiscard]]
static int slab_grow(struct slab_cache* cache)
{
	log_debug("Creating new slab for cache: %s", cache->name);
	void* base = (void*)slab_alloc_pages(cache->slab_size_pages);
	if (!base) {
		log_error("OOM growing slab for cache %s", cache->name);
		return -EOOM;
	}

#ifdef SLAB_DEBUG
	memset(base, POISON_PATTERN, cache->slab_size_pages * PAGE_SIZE); // Fill with a pattern for debugging
#endif

	struct slab* new_slab = (struct slab*)base;
	new_slab->parent = cache;
	new_slab->free_stack = kmalloc(cache->objects_per_slab * sizeof(void*));
	if (!new_slab->free_stack) {
		log_error("OOM growing slab for cache %s", cache->name);
		slab_free_pages(base, cache->slab_size_pages);
		return -EOOM;
	}

	new_slab->free_top = cache->objects_per_slab;
	log_debug("Free stack is %lu bytes and has a max of %lu objects", cache->objects_per_slab * sizeof(void*),
		  cache->objects_per_slab);

	// TODO: Try including the free_stack within the struct, removes extra allocation.
	// TODO: Try something like what the linux kernel does, where the free list is within the page,
	// because currently the list is on the same order of magnitude as the actual slab (depending on object size).
	for (size_t i = 0; i < cache->objects_per_slab; i++) {
		new_slab->free_stack[i] = (void*)((uintptr_t)base + cache->header_size + i * cache->object_size);
	}
	list_append(&cache->empty, &new_slab->link);
	log_debug("Initialized slab at %p", base);

	cache->total_slabs++;
	cache->total_objects += cache->objects_per_slab;

	return 0;
}

void slab_dump_stats(struct slab_cache* cache)
{
	if (!cache || cache->flags == CACHE_UNINITIALIZED) {
		log_error("Invalid or uninitialized cache");
		return;
	}

	log_info("Slab Cache Stats:");
	log_info("Name: %s", cache->name);
	log_info("Object Size: %lu", cache->object_size);
	log_info("Object Alignment: %lu", cache->object_align);
	log_info("Slab Size (pages): %lu", cache->slab_size_pages);
	log_info("Objects per Slab: %lu", cache->objects_per_slab);
	log_info("Header Size: %lu", cache->header_size);

	log_info("Total Slabs: %lu", cache->total_slabs);
	log_info("Total Objects: %lu", cache->total_objects);
	log_info("Used Objects: %lu", cache->used_objects);
}
