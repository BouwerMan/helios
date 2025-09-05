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

#undef LOG_LEVEL
#define LOG_LEVEL 1
#define FORCE_LOG_REDEF
#include <lib/log.h>
#undef FORCE_LOG_REDEF

#include <arch/cache.h>
#include <kernel/kmath.h>
#include <kernel/panic.h>
#include <kernel/spinlock.h>
#include <lib/string.h>
#include <mm/kmalloc.h>
#include <mm/page.h>
#include <mm/page_alloc.h>
#include <mm/slab.h>
#include <uapi/helios/errno.h>

/*******************************************************************************
* Global Variable Definitions
*******************************************************************************/

#ifdef SLAB_DEBUG
static constexpr char POISON_PATTERN = 0x5A;
static constexpr int POISON_BYTE_COUNT =
	16; // Number of bytes to verify at head/tail
static constexpr int REDZONE_SIZE =
	4;  // 4 bytes at head and tail (so it is technically cross platform :))
static constexpr long REDZONE_PATTERN = 0xDEADBEEF;
#else
static constexpr int POISON_PATTERN = 0;
static constexpr int POISON_BYTE_COUNT =
	0; // Number of bytes to verify at head/tail
static constexpr int REDZONE_SIZE =
	0; // 4 bytes at head and tail (so it is technically cross platform :))
static constexpr long REDZONE_PATTERN = 0;
#endif

// NOTE: All functions that deal with objects take in object start (not data_start which is object_start - REDZONE_SIZE)

/*******************************************************************************
* Private Function Prototypes
*******************************************************************************/

/**
 * @brief Grow the slab cache by allocating a new slab.
 *
 * @param cache Pointer to the slab_cache to grow.
 * @return 0 on success, -EOOM if out of memory.
 */
[[nodiscard]]
static int slab_grow(struct slab_cache* cache);

/**
 * @brief Destroy a single slab and release its memory.
 *
 * @param slab Pointer to the slab to destroy.
 */
static void slab_destroy(struct slab* slab);

/**
 * @brief Moves a slab to the quarantine state.
 *
 * @param slab Pointer to the slab structure to be quarantined.
 */
static void slab_quarantine(struct slab* slab);

/**
 * @brief Wrapper to allocate a contiguous block of pages for use in a slab.
 *
 * @param pages The number of contiguous pages to allocate.
 * @return Pointer to the allocated memory, or NULL if allocation fails.
 */
[[nodiscard, gnu::malloc, gnu::always_inline]]
static inline void* _slab_alloc_pages(size_t pages)
{
	return get_free_pages(AF_KERNEL, pages);
}

/**
 * @brief Wrapper to free a contiguous block of pages allocated for a slab.
 *
 * @param addr  Pointer to the starting address of the pages to free.
 * @param pages The number of contiguous pages to free.
 */
[[gnu::always_inline]]
static inline void _slab_free_pages(void* addr, size_t pages)
{
	free_pages(addr, pages);
}

/**
 * @brief Retrieve the slab structure from an object pointer.
 *
 * This function calculates the base address of the slab that contains the
 * given object. It uses address masking, which works because slabs are
 * allocated in power-of-2 sizes.
 *
 * @param object Pointer to the object whose slab is to be determined.
 * @return Pointer to the slab structure containing the object.
 */
[[gnu::always_inline]]
static inline struct slab* slab_from_object(const void* object)
{
	size_t slab_bytes =
		SLAB_SIZE_PAGES * PAGE_SIZE; // Total size of a slab in bytes
	uint64_t mask =
		~(slab_bytes - 1); // Mask to align the address to the slab base
	return (struct slab*)((uintptr_t)object & mask);
}

/**
 * @brief Relocates a slab to a new location within its cache.
 *
 * @param slab Pointer to the slab to be relocated.
 * @param location The new location to which the slab should be moved.
 */
static void slab_relocate(struct slab* slab, enum _SLAB_LOCATION location);

#if SLAB_DEBUG
static void __dump_data(const void* data, size_t size);
static bool check_poison(const void* obj_start, size_t size);
static bool check_redzone(const void* obj_start, size_t size);
#endif

static void test_use_before_alloc(struct slab_cache* cache);
static void test_buffer_overflow(struct slab_cache* cache);
static void test_buffer_underflow(struct slab_cache* cache);
static void test_valid_usage(struct slab_cache* cache);
static void test_object_alignment(struct slab_cache* cache);

/*******************************************************************************
* Public Function Definitions
*******************************************************************************/

/**
 * @brief Initialize a slab cache for fixed-size object allocations.
 *
 * This function sets up the metadata and free-list structures needed to manage
 * fixed-size object allocations. It ensures proper alignment, calculates the
 * number of objects per slab, and initializes the slab cache's internal lists.
 * Optional constructor and destructor callbacks can be registered for object
 * initialization and cleanup.
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
int slab_cache_init(struct slab_cache* cache,
		    const char* name,
		    size_t object_size,
		    size_t object_align,
		    void (*constructor)(void*),
		    void (*destructor)(void*))
{
	if (!cache) {
		log_error("Must supply valid cache structure pointer");
		return -EINVAL;
	}

	if (object_align == 0) {
		object_align = L1_CACHE_SIZE; // Default to L1 cache line size
		log_debug("Using default object alignment: %lu", object_align);
	} else {
		// Clamp the alignment to a minimum of sizeof(void*).
		// This is because freelist traversal breaks if the alignment is smaller than a pointer size.
		object_align = MAX(object_align, sizeof(void*));
	}

	if (!is_pow_of_two(object_align)) {
		log_error("Object alignment is not a power of 2: %lu",
			  object_align);
		return -EINVAL;
	}

	if (object_size >= PAGE_SIZE) {
		log_error(
			"Object size of %lu is basically the same size as a page.",
			object_size);
		return -ENOMEM; // Technically not OOM but idk what it should actually be
	}

	// Zero out cache just to make sure there is no garbage data there
	memset(cache, 0, sizeof(struct slab_cache));

	spin_init(&cache->lock);

	unsigned long flags;
	spin_lock_irqsave(&cache->lock, &flags);

	cache->object_size = object_size;

	/**
	 * We want each allocated object to be surrounded by redzones:
	 *     [ head redzone ][ aligned object ][ tail redzone ]
	 *
	 * However, inserting a redzone *before* the object complicates alignment.
	 * To ensure that the object itself is aligned to `object_align`, we do:
	 *
	 *     obj_start = ALIGN_UP(raw_ptr + REDZONE_SIZE, object_align);
	 *
	 * In the worst case, `raw_ptr + REDZONE_SIZE` is just shy of alignment,
	 * so the ALIGN_UP call could push the object forward by as much as
	 * (object_align - 1) bytes. To ensure that we always have enough space
	 * for the object and both redzones — regardless of alignment offset —
	 * we conservatively add `object_align` to the per-object stride.
	 *
	 * Therefore:
	 *
	 *     data_size = object_size + 2 * REDZONE_SIZE + object_align;
	 *
	 * This guarantees that each allocated region has enough space to
	 * accommodate both redzones and a properly aligned object.
	 */
	cache->data_size = object_size + 2 * REDZONE_SIZE + object_align;

	cache->object_align = object_align;
	cache->header_size = ALIGN_UP(sizeof(struct slab), object_align);
	cache->objects_per_slab =
		(SLAB_SIZE_PAGES * PAGE_SIZE - cache->header_size) /
		cache->data_size;

	list_init(&cache->empty);
	list_init(&cache->partial);
	list_init(&cache->full);
	list_init(&cache->quarantine);
	list_add_tail(&kernel.slab_caches, &cache->cache_node);

	cache->constructor = constructor;
	cache->destructor = destructor;

	// Copy the cache name and ensure null termination
	strncpy(cache->name, name, MAX_CACHE_NAME_LEN);
	cache->name[MAX_CACHE_NAME_LEN - 1] = '\0';

	cache->flags = CACHE_INITIALIZED;

	log_debug(
		"Cache '%s' initialized: object_size=%zu, data_size=%zu, object_align=%zu, header_size=%zu, objects_per_slab=%zu",
		cache->name,
		cache->object_size,
		cache->data_size,
		cache->object_align,
		cache->header_size,
		cache->objects_per_slab);

	spin_unlock_irqrestore(&cache->lock, flags);
	return 0;
}

/**
 * @brief Allocates an object from the specified slab cache.
 *
 * This function attempts to allocate an object from a slab cache. It first tries
 * to allocate from a partially filled slab. If no partially filled slabs are
 * available, it attempts to allocate from an empty slab or grow the cache by
 * creating a new slab. If all attempts fail, the function returns NULL.
 *
 * The function ensures thread safety by acquiring a spinlock during the allocation
 * process. If a constructor is defined for the cache, it is invoked on the allocated
 * object before returning it to the caller.
 *
 * @param cache Pointer to the slab cache from which to allocate an object.
 * @return Pointer to the allocated object, or NULL if allocation fails.
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

	unsigned long flags;
	spin_lock_irqsave(&cache->lock, &flags);

	log_debug("Asked to allocate from cache %s(%p) by caller %p",
		  cache->name,
		  (void*)cache,
		  __builtin_return_address(0));

retry:

	// Attempt to allocate from a partially filled slab
	if (!list_empty(&cache->partial)) {
		log_debug("Cache %s: Allocating from a partial slab",
			  cache->name);
		slab = list_first_entry(&cache->partial, struct slab, link);
	} else if (!list_empty(&cache->empty) ||
		   (res = slab_grow(cache)) >= 0) {
		// Move the first empty slab to the partial list
		slab = list_first_entry(&cache->empty, struct slab, link);
		slab_relocate(slab, SLAB_PARTIAL);
	} else {
		log_error("Could not create more slabs, slab_grow returned: %d",
			  res);
		spin_unlock_irqrestore(&cache->lock, flags);
		return NULL;
	}

	log_debug("Chose slab %p", (void*)slab);
	void* obj_start = slab->free_stack[--slab->free_top];

#ifdef SLAB_DEBUG
	bool quarantine = check_poison(obj_start, cache->object_size);
	if (!quarantine) {
		slab_quarantine(slab);
		goto retry; // If the object is poisoned, retry allocation
	}
#endif

	if (cache->constructor) cache->constructor(obj_start);

	// Move the slab to the full list if it becomes exhausted
	if (slab->free_top == 0) {
		slab_relocate(slab, SLAB_FULL);
	}

	log_debug(
		"Cache %s: allocated object %p from slab %p (free_top=%zu/%zu)",
		cache->name,
		obj_start,
		(void*)slab,
		slab->free_top,
		cache->objects_per_slab);

	cache->used_objects++;

	spin_unlock_irqrestore(&cache->lock, flags);
	return obj_start;
}

/**
 * @brief Return an object back to its slab cache.
 *
 * This function deallocates an object previously allocated by `slab_alloc` and
 * returns it to the appropriate slab in the cache. It identifies the parent slab,
 * optionally invokes the destructor, updates the slab's free-list and free-count,
 * and moves the slab between the full, partial, and empty lists as its occupancy
 * changes. If a slab becomes completely empty, it may be freed back to the page
 * allocator based on the cache's policy.
 *
 * The function ensures thread safety by acquiring a spinlock during the deallocation
 * process. Debugging features, such as redzone checks and memory poisoning, are
 * optionally enabled with the `SLAB_DEBUG` flag.
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

	unsigned long flags;
	spin_lock_irqsave(&cache->lock, &flags);

	struct slab* slab = slab_from_object(object);
	if (slab->parent != cache) {
		log_error(
			"Somehow got the wrong slab (parent doesn't match the cache), good luck debugging this one");
		spin_unlock_irqrestore(&cache->lock, flags);
		return;
	}

	if (slab->free_top >= cache->objects_per_slab) {
		log_error("Free top overflow for slab %p in cache %s",
			  (void*)slab,
			  cache->name);
		spin_unlock_irqrestore(&cache->lock, flags);
		return;
	}

	if (cache->destructor) cache->destructor(object);

	// If we are dubugging, we check for over or underflow into the redzone. Then we fill the object with the poison pattern.
#ifdef SLAB_DEBUG
	bool quarantine = check_redzone(object, cache->object_size);

	if (!quarantine) {
		slab_quarantine(slab);
		spin_unlock_irqrestore(&cache->lock, flags);
		return;
	}

	// Fill with a pattern for debugging
	memset(object, POISON_PATTERN, cache->object_size);
#endif

	slab->free_stack[slab->free_top++] = object;

	if (slab->free_top == cache->objects_per_slab) {
		slab_relocate(slab, SLAB_EMPTY);

		if (cache->num_empty > MAX_EMPTY_SLABS) {
			log_debug(
				"Cache %s: too many empty slabs, freeing slab %p",
				cache->name,
				(void*)slab);
			slab_destroy(slab);

			cache->num_empty--;
			cache->total_slabs--;
		}
	} else if (slab->free_top == 1) {
		slab_relocate(slab, SLAB_PARTIAL);
	}

	cache->used_objects--;

	log_debug("Cache %s: freed object %p to slab %p (free_top=%zu/%zu)",
		  cache->name,
		  object,
		  (void*)slab,
		  slab->free_top,
		  cache->objects_per_slab);

	spin_unlock_irqrestore(&cache->lock, flags);
}

/**
 * @brief Destroy a slab cache and release all its memory.
 *
 * This function walks through all slabs in the empty, partial, and full lists,
 * invoking the destructor on any remaining live objects. It returns each slab's
 * pages back to the underlying physical memory allocator and clears the cache's
 * metadata so it can be safely reinitialized or discarded.
 *
 * The function ensures thread safety by acquiring a spinlock during the destruction
 * process. It also logs debug information about the destruction process.
 *
 * @param cache Pointer to the slab_cache to destroy. Must be a valid, initialized cache.
 */
void slab_cache_destroy(struct slab_cache* cache)
{
	if (!cache) {
		log_error(
			"I can't destroy a cache if you don't give me a valid cache");
		return;
	}
	if (cache->flags == CACHE_UNINITIALIZED) {
		log_error("Supplied uninitialized cache");
		return;
	}

	// Never release it because we are deleting everything anyways
	spin_lock(&cache->lock);

	log_debug("Destroying cache %s", cache->name);

	struct slab* slab;

	// Full slabs
	while (!list_empty(&cache->full)) {
		slab = list_entry(cache->full.next, struct slab, link);
		slab_destroy(slab);
	}

	// Partial slabs
	while (!list_empty(&cache->partial)) {
		slab = list_entry(cache->partial.next, struct slab, link);
		slab_destroy(slab);
	}

	// Empty slabs
	while (!list_empty(&cache->empty)) {
		slab = list_entry(cache->empty.next, struct slab, link);
		slab_destroy(slab);
	}

	list_del(&cache->cache_node);

	// This also sets the flags to CACHE_UNINITIALIZED
	memset(cache, 0, sizeof(struct slab_cache));
}

/**
 * @brief Purge all corrupt slabs from the quarantine list of a slab cache.
 *
 * This function iterates through the quarantine list of a slab cache and destroys
 * all slabs marked as corrupt. It updates the cache's metadata to reflect the
 * removal of these slabs and logs detailed debug information during the process.
 *
 * The function performs the following steps:
 * 1. Iterates through the quarantine list of the cache.
 * 2. Destroys each slab in the quarantine list using `_slab_destroy`.
 * 3. Updates the `num_quarantine` and `total_objects` counters in the cache.
 * 4. Logs debug information about the purge process.
 *
 * @param cache Pointer to the slab_cache whose corrupt slabs are to be purged.
 */
void slab_cache_purge_corrupt(struct slab_cache* cache)
{
	log_debug("Starting purge of corrupt slabs in cache '%s'", cache->name);

	struct slab* slab;
	while (!list_empty(&cache->quarantine)) {
		slab = list_entry(cache->quarantine.next, struct slab, link);
		log_debug("Purging slab at %p from quarantine", (void*)slab);

		slab_destroy(slab);
		cache->num_quarantine--;

		// Manually re-add total_objects since we double subtracted during quarantine and destruction
		cache->total_objects += cache->objects_per_slab;
		log_debug(
			"Updated cache '%s': num_quarantine=%zu, total_objects=%zu",
			cache->name,
			cache->num_quarantine,
			cache->total_objects);
	}

	log_debug("Completed purge of corrupt slabs in cache '%s'",
		  cache->name);
}

/**
 * @brief Dump statistics of a slab cache for debugging purposes.
 *
 * This function logs detailed information about the state of a slab cache,
 * including its name, object size, alignment, slab size, and usage statistics.
 *
 * @param cache Pointer to the slab_cache whose statistics are to be dumped.
 *              If the cache is NULL or uninitialized, an error is logged.
 */
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
	log_info("Slab Size (pages): %d", SLAB_SIZE_PAGES);
	log_info("Objects per Slab: %lu", cache->objects_per_slab);
	log_info("Header Size: %lu", cache->header_size);

	log_info("Num Empty Slabs: %lu", cache->num_empty);
	log_info("Num Partial Slabs: %lu", cache->num_partial);
	log_info("Num Full Slabs: %lu", cache->num_full);
	log_info("Num Quarantine Slabs: %lu", cache->num_quarantine);

	log_info("Total Slabs: %lu", cache->total_slabs);
	log_info("Total Objects: %lu", cache->total_objects);
	log_info("Used Objects: %lu", cache->used_objects);
}

void slab_test()
{
	log_info(TESTING_HEADER, "Slab Allocator");

	struct slab_cache test_cache = { 0 };
	(void)slab_cache_init(
		&test_cache, "Test cache", sizeof(uint64_t), 0, NULL, NULL);
	log_debug("Test cache slab size: %d pages", SLAB_SIZE_PAGES);

	test_use_before_alloc(&test_cache);
	test_buffer_overflow(&test_cache);
	test_buffer_underflow(&test_cache);
	test_valid_usage(&test_cache);
	test_object_alignment(&test_cache);

	slab_cache_purge_corrupt(&test_cache);

	uint64_t* data = slab_alloc(&test_cache);
	*data = 12345;
	log_info("Got data at %p, set value to %lu", (void*)data, *data);
	uint64_t* data2 = slab_alloc(&test_cache);
	*data2 = 54321;
	log_info("Got data2 at %p, set value to %lu", (void*)data2, *data2);
	size_t slab_bytes = SLAB_SIZE_PAGES * PAGE_SIZE;
	size_t mask = ~(slab_bytes - 1);
	log_debug("Slab base for data: %lx", (uintptr_t)data & mask);
	slab_dump_stats(&test_cache);
	slab_free(&test_cache, data2);

	slab_cache_destroy(&test_cache);
	(void)slab_alloc(&test_cache);
	slab_dump_stats(&test_cache);

	log_info(TESTING_FOOTER, "Slab Allocator");
}

/*******************************************************************************
* Private Function Definitions
*******************************************************************************/

static void slab_destroy(struct slab* slab)
{
	struct slab_cache* cache = slab->parent;
	log_debug("Cache %s: Destroying slab %p", cache->name, (void*)slab);

	void* base = (void*)slab;
	uintptr_t data_base = (uintptr_t)base + cache->header_size;
	size_t N = cache->objects_per_slab;

	if (slab->free_top == 0) {
		// Full slab, destructor called on all objects
		for (size_t i = 0; i < N; i++) {
			uintptr_t raw_ptr = data_base + i * cache->data_size;
			void* obj = (void*)ALIGN_UP(raw_ptr + REDZONE_SIZE,
						    cache->object_align);
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
			size_t idx =
				(ptr - (uintptr_t)base - cache->header_size) /
				cache->data_size;
			is_free[idx] = true;
		}
		// Now call destructor on each non free item
		for (size_t i = 0; i < N; i++) {
			if (!is_free[i]) {
				uintptr_t raw_ptr =
					data_base + i * cache->data_size;
				void* obj =
					(void*)ALIGN_UP(raw_ptr + REDZONE_SIZE,
							cache->object_align);
				if (cache->destructor) cache->destructor(obj);
			}
		}
		kfree(is_free);
	}

	list_del(&slab->link);
	kfree(slab->free_stack);
	_slab_free_pages(base, SLAB_SIZE_PAGES);

	cache->total_slabs--;
	cache->total_objects -= cache->objects_per_slab;
}

[[nodiscard]]
static int slab_grow(struct slab_cache* cache)
{
	log_debug("Creating new slab for cache: %s(%p)",
		  cache->name,
		  (void*)cache);
	void* base = (void*)_slab_alloc_pages(SLAB_SIZE_PAGES);
	if (!base) {
		log_error("OOM growing slab for cache %s", cache->name);
		return -ENOMEM;
	}

#ifdef SLAB_DEBUG
	memset(base,
	       POISON_PATTERN,
	       SLAB_SIZE_PAGES *
		       PAGE_SIZE); // Fill with a pattern for debugging
#endif

	struct slab* new_slab = (struct slab*)base;
	memset(new_slab, 0, sizeof(struct slab)); // Zero out the slab metadata
	new_slab->parent = cache;
	new_slab->free_stack = kmalloc(cache->objects_per_slab * sizeof(void*));
	if (!new_slab->free_stack) {
		log_error("OOM growing slab for cache %s", cache->name);
		_slab_free_pages(base, SLAB_SIZE_PAGES);
		return -ENOMEM;
	}

	new_slab->free_top = cache->objects_per_slab;
	log_debug("Free stack is %lu bytes and has a max of %lu objects",
		  cache->objects_per_slab * sizeof(void*),
		  cache->objects_per_slab);

	// TODO: Try including the free_stack within the struct, removes extra allocation.
	// TODO: Try something like what the linux kernel does, where the free list is within the page,
	// because currently the list is on the same order of magnitude as the actual slab (depending on object size).
	for (size_t i = 0; i < cache->objects_per_slab; i++) {
		uintptr_t data_base = (uintptr_t)base + cache->header_size;
		uintptr_t raw_ptr = data_base + i * cache->data_size;

		// Align the object such that obj_start is aligned, with redzone before it
		uintptr_t obj_start =
			ALIGN_UP(raw_ptr + REDZONE_SIZE, cache->object_align);
#ifdef SLAB_DEBUG
		uint32_t* redzone_head = (uint32_t*)(obj_start - REDZONE_SIZE);
		*redzone_head = REDZONE_PATTERN; // Set the redzone at the start

		uint32_t* redzone_tail =
			(uint32_t*)(obj_start + cache->object_size);
		*redzone_tail = REDZONE_PATTERN; // Set the redzone at the end
#endif
		new_slab->free_stack[i] = (void*)obj_start;
	}

	new_slab->location = SLAB_EMPTY;
	cache->num_empty++;
	cache->total_slabs++;
	cache->total_objects += cache->objects_per_slab;

	list_add_tail(&cache->empty, &new_slab->link);
	log_debug("Initialized slab (%p) at base: %p", (void*)new_slab, base);

	return 0;
}

static void slab_quarantine(struct slab* slab)
{
	struct slab_cache* cache = slab->parent;
	slab_relocate(slab, SLAB_QUARANTINE);

	cache->used_objects -= cache->objects_per_slab - slab->free_top;
	cache->total_objects -= cache->objects_per_slab;

	log_warn("Cache %s: slab %p moved to quarantine",
		 cache->name,
		 (void*)slab);
}

static void slab_relocate(struct slab* slab, enum _SLAB_LOCATION location)
{
	if (!slab) return;

	struct slab_cache* cache = slab->parent;

	size_t* const counters[] = {
		[SLAB_EMPTY] = &cache->num_empty,
		[SLAB_PARTIAL] = &cache->num_partial,
		[SLAB_FULL] = &cache->num_full,
		[SLAB_QUARANTINE] = &cache->num_quarantine,
	};
	struct list_head* const lists[] = {
		[SLAB_EMPTY] = &cache->empty,
		[SLAB_PARTIAL] = &cache->partial,
		[SLAB_FULL] = &cache->full,
		[SLAB_QUARANTINE] = &cache->quarantine,
	};

	(*counters[slab->location])--;
	(*counters[location])++;

	// Move the slab to the new list
	list_move(&slab->link, lists[location]);

	log_debug("Cache %s: slab %p moved from %d to %d.",
		  cache->name,
		  (void*)slab,
		  slab->location,
		  location);

	// Update the slab's location
	slab->location = location;
}

#if SLAB_DEBUG
// Pass through object_start - REDZONE_SIZE
[[maybe_unused]]
static void __dump_data(const void* data, size_t size)
{
	for (size_t i = 0; i < size; i++) {
		printf("%hhx ", ((uint8_t*)data)[i]);
	}
	printf("\n");
}

// #define SLAB_DEBUG_VERBOSE
#ifdef SLAB_DEBUG_VERBOSE
#define _dump_data(data, size) __dump_data(data, size)
#else
#define _dump_data(data, size) ((void)0)
#endif

/**
 * @brief Check for memory poisoning in a slab object.
 *
 * This function verifies that the memory region of an object is properly
 * poisoned with a predefined pattern. It checks both the head and tail of
 * the object for the poison pattern to detect use-before-initialization
 * or memory corruption.
 *
 * If a mismatch is detected, an error is logged, the slab is marked as
 * corrupted, and debugging information is dumped.
 *
 * @param obj_start Pointer to the start of the object to check.
 * @param size      Size of the object in bytes.
 * @return true if the object is correctly poisoned, false otherwise.
 */
static bool check_poison(const void* obj_start, size_t size)
{
	struct slab* slab;
	const uint8_t* byte_ptr = (const uint8_t*)((uintptr_t)obj_start);

	// How many bytes to check at head and tail?
	size_t check_len = POISON_BYTE_COUNT;
	if (check_len * 2 > size) {
		check_len = size / 2; // round down if not enough space
	}

	for (size_t i = 0; i < check_len; i++) {
		if (byte_ptr[i] != POISON_PATTERN) {
			log_error(
				"Use-before-init detected at start of object at byte %zu",
				i);
			_dump_data(obj_start, size);
			slab = slab_from_object(obj_start);
			slab->debug_error = true;
			return false;
		}
		if (byte_ptr[size - 1 - i] != POISON_PATTERN) {
			log_error(
				"Use-before-init detected at end of object at byte %zu",
				size - 1 - i);
			_dump_data(obj_start, size);
			slab = slab_from_object(obj_start);
			slab->debug_error = true;
			return false;
		}
	}

	return true;
}

/**
 * @brief Check for redzone violations in a slab object.
 *
 * This function verifies that the redzones surrounding an object are intact.
 * Redzones are special memory regions placed before and after an object to
 * detect buffer overflows and underflows. If a redzone violation is detected,
 * an error is logged, debugging information is dumped, and the slab is marked
 * as corrupted.
 *
 * The function performs the following checks:
 * 1. Verifies the integrity of the redzone before the object (underflow check).
 * 2. Verifies the integrity of the redzone after the object (overflow check).
 * 3. Resets the redzone patterns if a violation is detected.
 *
 * @param obj_start Pointer to the start of the object to check.
 * @param size      Size of the object in bytes.
 * @return true if no redzone violations are detected, false otherwise.
 */
static bool check_redzone(const void* obj_start, size_t size)
{
	struct slab* slab = slab_from_object((void*)obj_start);

	uint32_t* redzone_start =
		(uint32_t*)((uintptr_t)obj_start - REDZONE_SIZE);
	if (*redzone_start != REDZONE_PATTERN) {
		log_error("Underflow on freed object detected");
		_dump_data((void*)((uintptr_t)obj_start - REDZONE_SIZE),
			   slab->parent->data_size);
		*redzone_start =
			REDZONE_PATTERN;  // Reset the redzone at the start
		slab->debug_error = true; // Mark slab as corrupted
	}

	uint32_t* redzone_end = (uint32_t*)((uintptr_t)obj_start + size);
	if (*redzone_end != REDZONE_PATTERN) {
		log_error("Overflow on freed object detected");
		_dump_data((void*)((uintptr_t)obj_start - REDZONE_SIZE),
			   slab->parent->data_size);
		*redzone_end = REDZONE_PATTERN; // Reset the redzone at the end
		slab->debug_error = true;	// Mark slab as corrupted
	}

	return !slab->debug_error; // Return true if no errors found
}
#endif

/**
 * @brief Test for use-before-initialization in a slab cache.
 *
 * This function simulates a use-before-initialization scenario by accessing
 * an object from the slab cache before it is properly allocated. It ensures
 * that the slab cache's debugging mechanisms detect and report the corruption.
 *
 * The test performs the following steps:
 * 1. Ensures the cache has at least one slab by growing it if necessary.
 * 2. Accesses an object from the free stack and writes to it before allocation.
 * 3. Allocates the object using `slab_alloc` and reinserts it manually.
 * 4. Verifies that the slab is marked as corrupted due to the use-before-init.
 * 5. Resets the debug error flag for further tests.
 *
 * @param cache Pointer to the slab_cache to test.
 */
static void test_use_before_alloc(struct slab_cache* cache)
{
	log_info("Testing use-before-init in slab cache");
	if (list_empty(&cache->empty)) {
		(void)slab_grow(cache); // Ensure we have at least one slab
	}
	struct slab* slab = list_entry(cache->empty.next, struct slab, link);
	kassert(slab->free_top > 0);
	void* poisoned_obj =
		(void*)((uintptr_t)slab->free_stack[slab->free_top - 1]);

	// simulate use-before-init:
	((uint8_t*)poisoned_obj)[0] = 0xAA;
	void* obj = slab_alloc(cache);

	// reinsert manually for test
	slab_free(cache, obj);

	kassert(slab->debug_error == true &&
		"Slab should be marked as corrupted after use-before-init");
	log_info("Use-before-init test passed.");
	slab->debug_error = false; // Reset for further tests
}

/**
 * @brief Test for buffer overflow detection in a slab cache.
 *
 * This function simulates a buffer overflow scenario by writing beyond the
 * allocated object's boundary into the redzone. It ensures that the slab
 * cache's debugging mechanisms detect and report the corruption.
 *
 * The test performs the following steps:
 * 1. Allocates an object from the slab cache.
 * 2. Writes past the end of the object into the redzone.
 * 3. Frees the object, expecting the slab cache to detect the overflow.
 * 4. Verifies that the slab is marked as corrupted due to the overflow.
 * 5. Resets the debug error flag for further tests.
 *
 * @param cache Pointer to the slab_cache to test.
 */
static void test_buffer_overflow(struct slab_cache* cache)
{
	log_info("Testing buffer overflow detection in slab cache");
	void* obj = slab_alloc(cache);
	struct slab* slab = slab_from_object(obj);

	// Write past the end of the object (into redzone)
	((uint8_t*)obj)[cache->object_size] = 0xAB;

	slab_free(cache, obj); // Should log "Overflow on freed object detected"

	kassert(slab->debug_error == true &&
		"Slab should be marked as corrupted after overflow");
	log_info("Buffer overflow test passed.");
	slab->debug_error = false; // Reset for further tests
}

/**
 * @brief Test for buffer underflow detection in a slab cache.
 *
 * This function simulates a buffer underflow scenario by writing before the
 * allocated object's boundary into the redzone. It ensures that the slab
 * cache's debugging mechanisms detect and report the corruption.
 *
 * The test performs the following steps:
 * 1. Allocates an object from the slab cache.
 * 2. Writes just before the start of the object into the redzone.
 * 3. Frees the object, expecting the slab cache to detect the underflow.
 * 4. Verifies that the slab is marked as corrupted due to the underflow.
 * 5. Resets the debug error flag for further tests.
 *
 * @param cache Pointer to the slab_cache to test.
 */
static void test_buffer_underflow(struct slab_cache* cache)
{
	log_info("Testing buffer underflow detection in slab cache");
	void* obj = slab_alloc(cache);
	struct slab* slab = slab_from_object(obj);

	// Write just before the object (into the redzone)
	((uint8_t*)obj)[-1] = 0xBA;

	slab_free(cache,
		  obj); // Should log "Underflow on freed object detected"

	kassert(slab->debug_error == true &&
		"Slab should be marked as corrupted after underflow");
	log_info("Buffer underflow test passed.");
	slab->debug_error = false; // Reset for further tests
}

/**
 * @brief Test valid usage of a slab cache.
 *
 * This function verifies that the slab cache operates correctly under normal
 * usage conditions. It allocates an object, performs valid operations on it,
 * and then frees it. The test ensures that no warnings or errors are triggered
 * and that the slab is not marked as corrupted.
 *
 * The test performs the following steps:
 * 1. Allocates an object from the slab cache.
 * 2. Writes to the object within its allocated bounds.
 * 3. Frees the object back to the slab cache.
 * 4. Verifies that the slab is not marked as corrupted.
 *
 * @param cache Pointer to the slab_cache to test.
 */
static void test_valid_usage(struct slab_cache* cache)
{
	log_info("Testing valid usage of slab cache");
	void* obj = slab_alloc(cache);
	struct slab* slab = slab_from_object(obj);
	memset(obj, 0, cache->object_size); // Legal usage

	slab_free(cache, obj); // Should not trigger any warnings or logs
	kassert(slab->debug_error == false &&
		"Slab should not be marked as corrupted after valid usage");
	log_info("Valid usage test passed.");
}

/**
 * @brief Test object alignment in a slab cache.
 *
 * This function verifies that all objects allocated from the slab cache are
 * properly aligned according to the cache's specified alignment. It allocates
 * multiple objects, checks their alignment, and logs any misaligned objects.
 * If a misalignment is detected, the function triggers an assertion failure.
 *
 * The test performs the following steps:
 * 1. Allocates 32 objects from the slab cache.
 * 2. Checks the alignment of each object's address.
 * 3. Logs an error and asserts if any object is not properly aligned.
 * 4. Frees each allocated object back to the slab cache.
 * 5. Logs a success message if all objects are properly aligned.
 *
 * @param cache Pointer to the slab_cache to test.
 */
static void test_object_alignment(struct slab_cache* cache)
{
	log_info("Testing object alignment in slab cache");

	for (size_t i = 0; i < 32; i++) {
		void* obj = slab_alloc(cache);
		uintptr_t addr = (uintptr_t)obj;

		if (addr % cache->object_align != 0) {
			log_error("Object at %p is not aligned to %lu",
				  obj,
				  cache->object_align);
			kassert(false && "Slab object is not properly aligned");
		}

		slab_free(cache, obj);
	}

	log_info("Object alignment test passed for alignment=%lu",
		 cache->object_align);
}
