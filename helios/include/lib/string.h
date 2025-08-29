/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <kernel/types.h>
#include <stddef.h>

#define __STRING_H_CHECK_ALIGN(num, dest, src, size) \
	((num % size == 0) && (dest % size == 0) && (src % size == 0))

extern void* memset64(uint64_t* s, uint64_t v, size_t n);

void* memcpy(void* restrict s1, const void* restrict s2, size_t n);
void* memmove(void* s1, const void* s2, size_t n);
char* strcpy(char* restrict s1, const char* restrict s2);
char* strncpy(char* restrict s1, const char* restrict s2, size_t n);
char* strdup(const char* s);
char* strndup(const char* s, size_t n);
char* strcat(char* restrict s1, const char* restrict s2);
char* strncat(char* restrict s1, const char* restrict s2, size_t n);

int memcmp(const void* s1, const void* s2, size_t n);

int strcmp(const char* s1, const char* s2);

int strncmp(const char* s1, const char* s2, size_t n);

[[gnu::pure]]
char* strchr(const char* s, int c);
char* strrchr(const char* s, int c);

char* strtok(char* restrict s1, const char* restrict s2);

size_t strlen(const char*);

extern void* memset(void* s, int c, size_t n);

size_t strnlen_s(const char* s, size_t n);
size_t strnlen(const char* s, size_t n);
