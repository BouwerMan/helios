/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <lib/string.h>
#include <stddef.h>

// NOTE: This is from liballoc_1_1 at https://github.com/blanham/liballoc/tree/master

/** \defgroup ALLOCHOOKS liballoc hooks
 *
 * These are the OS specific functions which need to
 * be implemented on any platform that the library
 * is expected to work on.
 */

/** @{ */

// If we are told to not define our own size_t, then we skip the define.
// #define _HAVE_UINTPTR_T
// typedef	unsigned long	uintptr_t;

/// Used to init the spinlock
extern void liballoc_init();

/** This function is supposed to lock the memory data structures. It
 * could be as simple as disabling interrupts or acquiring a spinlock.
 * It's up to you to decide.
 *
 * \return 0 if the lock was acquired successfully. Anything else is
 * failure.
 */
extern int liballoc_lock();

/** This function unlocks what was previously locked by the liballoc_lock
 * function.  If it disabled interrupts, it enables interrupts. If it
 * had acquiried a spinlock, it releases the spinlock. etc.
 *
 * \return 0 if the lock was successfully released.
 */
extern int liballoc_unlock();

/** This is the hook into the local system which allocates pages. It
 * accepts an integer parameter which is the number of pages
 * required.  The page size was set up in the liballoc_init function.
 *
 * \return NULL if the pages were not allocated.
 * \return A pointer to the allocated memory.
 */
extern void* liballoc_alloc(size_t);

/** This frees previously allocated memory. The void* parameter passed
 * to the function is the exact same value returned from a previous
 * liballoc_alloc call.
 *
 * The integer value is the number of pages to free.
 *
 * \return 0 if the memory was successfully freed.
 */
extern int liballoc_free(void*, size_t);

[[nodiscard, gnu::malloc, gnu::alloc_size(1), gnu::nothrow]]
extern void* kmalloc(size_t);	      ///< The standard function.

[[nodiscard, gnu::alloc_size(2), gnu::nothrow]]
extern void* krealloc(void*, size_t); ///< The standard function.

[[nodiscard, gnu::malloc, gnu::alloc_size(1, 2), gnu::nothrow]]
extern void* kcalloc(size_t, size_t); ///< The standard function.

extern void kfree(void*);	      ///< The standard function.

[[gnu::malloc, gnu::alloc_size(1), gnu::nothrow]]
static inline void* kzalloc(size_t size)
{
	void* m = kmalloc(size);

	if (!m) {
		return NULL;
	}

	return memset(m, 0, size);
}

/** @} */
