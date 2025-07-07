#ifndef _STDLIB_H
#define _STDLIB_H
#pragma once

#define __need_size_t
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

__attribute__((__noreturn__)) void abort(void);
int atexit(void (*func)(void));
int atoi(const char* nptr);
char* getenv(const char* name);

#include <liballoc.h>
#if defined(__is_libk)
#define LIBC_MALLOC(size) kmalloc(size)
#define LIBC_FREE(ptr)	  kfree(ptr)
#else
#define LIBC_MALLOC(size) malloc(size)
#define LIBC_FREE(ptr)	  free(ptr)
#endif
// void free(void* ptr);
// void* malloc(size_t size);

#ifdef __cplusplus
}
#endif

#endif /* _STDLIB_H */
