/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#if defined(__is_libk)
#define LIBC_MALLOC(size) kmalloc(size)
#define LIBC_FREE(ptr)	  kfree(ptr)
#else
#define LIBC_MALLOC(size) malloc(size)
#define LIBC_FREE(ptr)	  free(ptr)
#endif
