#ifndef _STDLIB_H
#define _STDLIB_H

#define __need_size_t
#include <stddef.h>

#include <features.h>

#ifdef __cplusplus
extern "C" {
#endif

int atoi(const char* __nptr) __nothrow;

void abort(void) __noreturn __nothrow;

int atexit(void (*__func)(void)) __nothrow;

char* getenv(const char* __name) __nothrow;

void exit(int __status) __noreturn __nothrow;

// Memory allocation functions

void* malloc(size_t __size) __malloc
	__alloc_size(1) __nothrow __warn_unused_result;

void* calloc(size_t __nmemb, size_t __size) __malloc
	__alloc_size(1, 2) __nothrow __warn_unused_result;

void* realloc(void* __ptr, size_t __size)
	__alloc_size(2) __nothrow __warn_unused_result;

void free(void* __ptr) __nothrow __nonnull(1);

#ifdef __cplusplus
}
#endif

#endif /* _STDLIB_H */
