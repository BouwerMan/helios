/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _STRING_H
#define _STRING_H 1
#pragma once

#include <features.h>

#define __need_NULL
#define __need_size_t
#include <stddef.h>

/// Checks the alignment of dest and src while making sure num can be evenly divisible
#define __STRING_H_CHECK_ALIGN(num, dest, src, size) ((num % size == 0) && (dest % size == 0) && (src % size == 0))

#ifdef __cplusplus
extern "C" {
#endif

void* memcpy(void* restrict s1, const void* restrict s2, size_t n);
// FIXME: Unimplemented
void* memccpy(void* restrict s1, const void* restrict s2, int c, size_t n);
void* memmove(void* s1, const void* s2, size_t n);
char* strcpy(char* restrict s1, const char* restrict s2);
char* strncpy(char* restrict s1, const char* restrict s2, size_t n);
char* strdup(const char* s);
char* strndup(const char* s, size_t n);
char* strcat(char* restrict s1, const char* restrict s2);
char* strncat(char* restrict s1, const char* restrict s2, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);
int strcmp(const char* s1, const char* s2);
// FIXME: Unimplemented
int strcoll(const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, size_t n);
// FIXME: Unimplemented
size_t strxfrm(char* restrict s1, const char* restrict s2, size_t n);
// FIXME: Unimplemented
void* memchr(const void* s, int c, size_t n);
// FIXME: Unimplemented
char* strchr(const char* s, int c);
// FIXME: Unimplemented
size_t strcspn(const char* s1, const char* s2);
// FIXME: Unimplemented
char* strpbrk(const char* s1, const char* s2);
// FIXME: Unimplemented
size_t strspn(const char* s1, const char* s2);
// FIXME: Unimplemented
char* strstr(const char* s1, const char* s2);
char* strtok(char* restrict s1, const char* restrict s2);
void* memset(void* s, int c, size_t n);
// FIXME: Unimplemented
void* memset_explicit(void* s, int c, size_t n);
// FIXME: Unimplemented
char* strerror(int errnum);
size_t strlen(const char*);

#ifdef __STDC_WANT_LIB_EXT1__
size_t strnlen_s(const char* s, size_t n);
size_t strnlen(const char* s, size_t n);
#endif

#ifdef __cplusplus
}
#endif

#endif
