#pragma once

#include <stdlib.h>

int __atoi(const char* nptr);

void __abort(void) __noreturn;

char* __getenv(const char* name);

void* __malloc(size_t size) __malloc_like
	__alloc_size(1) __nothrow __warn_unused_result;

void* __calloc(size_t nmemb, size_t size) __malloc_like
	__alloc_size(1, 2) __nothrow __warn_unused_result;

void* __realloc(void* ptr, size_t size)
	__alloc_size(2) __nothrow __warn_unused_result;

void __free(void* ptr) __nothrow __nonnull(1);
