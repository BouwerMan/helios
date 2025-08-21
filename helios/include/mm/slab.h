/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stddef.h>

#include <kernel/helios.h>
#include <kernel/spinlock.h>

#include <util/list.h>

// TODO: Try using a free list stored in the freed object's slab slice similar to the linux kernel

static constexpr int MAX_CACHE_NAME_LEN = 32;

// NOTE: SLAB_SIZE_PAGES must be a power of 2.
static constexpr int SLAB_SIZE_PAGES = 16;

// Maximum number of free slabs that a cache can have before it starts destroying them.
static constexpr int MAX_EMPTY_SLABS = 8;

enum slab_cache_flags {
	CACHE_UNINITIALIZED = 0,
	CACHE_INITIALIZED,
};

enum _SLAB_LOCATION {
	SLAB_EMPTY = 0,	  /**< Slab is empty with no allocated objects. */
	SLAB_PARTIAL = 1, /**< Slab has some allocated objects but not full. */
	SLAB_FULL = 2,	  /**< Slab is fully occupied with no free objects. */
	SLAB_QUARANTINE = 3,
};

/**
 * @brief Represents a slab cache for managing memory allocation of fixed-size objects.
 *
 * A slab cache is a memory management structure that organizes memory into slabs,
 * each containing multiple objects of the same size. This structure holds metadata
 * about the cache, including object size, alignment, free lists, and statistics.
 */
struct slab_cache {
	/** @brief Size of the object allocated and referenced by the caller. */
	size_t object_size;
	/** @brief Size of the data area in the slab (object_size + debug data). */
	size_t data_size;
	/** @brief Alignment boundary for each object. */
	size_t object_align;
	/** @brief Number of objects that can fit in a single slab. */
	size_t objects_per_slab;
	/** @brief Size of the slab header metadata. */
	size_t header_size;
	/** @brief Flags indicating the state or behavior of the slab cache. */
	enum slab_cache_flags flags;

	/** @brief Spinlock for ensuring thread safety during operations. */
	spinlock_t lock;

	/** @brief List of empty slabs with no allocated objects. */
	struct list_head empty;
	/** @brief Number of empty slabs. */
	size_t num_empty;
	/** @brief List of partially filled slabs with some allocated objects. */
	struct list_head partial;
	/** @brief Number of partially filled slabs. */
	size_t num_partial;
	/** @brief List of fully occupied slabs with no free objects. */
	struct list_head full;
	/** @brief Number of fully occupied slabs. */
	size_t num_full;
	/** @brief List of slabs in quarantine for debugging use-after-free errors. */
	struct list_head quarantine;
	/** @brief Number of slabs currently in the quarantine list. */
	size_t num_quarantine;
	/** @brief Link to the slab cache node in the global list of caches. */
	struct list_head cache_node;

	/** @brief Constructor callback for initializing objects in the cache. */
	void (*constructor)(void*);
	/** @brief Destructor callback for cleaning up objects in the cache. */
	void (*destructor)(void*);

	/** @brief Total number of active slabs in the cache. */
	size_t total_slabs;
	/** @brief Total number of objects managed by all slabs in the cache. */
	size_t total_objects;
	/** @brief Number of currently allocated (live) objects in the cache. */
	size_t used_objects;

	/** @brief Human-readable name of the slab cache. */
	char name[MAX_CACHE_NAME_LEN];
};

/**
 * @brief Represents a slab in the slab allocator.
 *
 * A slab is a contiguous block of memory that contains multiple objects of the
 * same size. This structure holds metadata about the slab, including its parent
 * cache, free object stack, and debugging information.
 */
struct slab {
	/** @brief Link to the next slab in the list. */
	struct list_head link;
	/** @brief Index of the top of the free stack. */
	size_t free_top;
	/** @brief Pointer to the parent slab cache. */
	struct slab_cache* parent;
	/** @brief Stack of free object pointers within the slab. */
	void** free_stack;
	/** @brief Indicates which list the slab is currently in (empty, partial, full, quarantine). */
	enum _SLAB_LOCATION location;

#if SLAB_DEBUG
	/** @brief Indicates if the slab is marked as poisoned or corrupted. */
	bool debug_error;
#endif
};

/**
 * @brief Initialize a slab cache for fixed-size object allocations.
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
		    void (*destructor)(void*));

/**
 * @brief Destroy a slab cache and release all its memory.
 *
 * @param cache Pointer to the slab_cache to destroy. Must be a valid, initialized cache.
 */
void slab_cache_destroy(struct slab_cache* cache);

/*******************************************************************************
 *
 * Allocation and Deallocation Functions
 *
 *******************************************************************************/

/**
 * @brief Allocates an object from the specified slab cache.
 *
 * @param cache Pointer to the slab cache from which to allocate an object.
 *
 * @return Pointer to the allocated object, or NULL if allocation fails.
 */
[[nodiscard, gnu::malloc]]
void* slab_alloc(struct slab_cache* cache);

/**
 * @brief Return an object back to its slab cache.
 *
 * @param cache  Pointer to the slab_cache managing the object.
 * @param object Pointer to the object to free; must have originated from this cache.
 */
void slab_free(struct slab_cache* cache, void* object);

/**
 * @brief Purge all corrupt slabs from the quarantine list of a slab cache.
 *
 * @param cache Pointer to the slab_cache whose corrupt slabs are to be purged.
 */
void slab_cache_purge_corrupt(struct slab_cache* cache);

/*******************************************************************************
*
* TESTING FUNCTIONS
*
*******************************************************************************/

void slab_test();

/**
 * @brief Dump statistics of a slab cache for debugging purposes.
 *
 * @param cache Pointer to the slab_cache whose statistics are to be dumped.
 *              If the cache is NULL or uninitialized, an error is logged.
 */
void slab_dump_stats(struct slab_cache* cache);
