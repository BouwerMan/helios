#ifndef _STDLIB_H
#define _STDLIB_H
#pragma once

#define __need_size_t
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

extern char** environ;

__attribute__((__noreturn__)) void abort(void);
int atexit(void (*func)(void));
int atoi(const char* nptr);
char* getenv(const char* name);
[[noreturn]] void exit(int status);

#include <liballoc.h>
// void free(void* ptr);
// void* malloc(size_t size);

#ifdef __cplusplus
}
#endif

#endif /* _STDLIB_H */
