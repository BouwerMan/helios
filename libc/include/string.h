#pragma once

#include <sys/cdefs.h>

#include <stddef.h>

size_t strlen(const char*);
void* memset(void*, int, size_t);
void* memcpy(void* dest, const void* src, size_t count);
int strcmp(const char* str1, const char* str2);
int strncmp(const char* str1, const char* str2, size_t count);
