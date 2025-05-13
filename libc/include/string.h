#pragma once

#include <stdint.h>
#include <sys/cdefs.h>

#include <stddef.h>

/// Checks the alignment of dest and src while making sure num can be evenly divisible
#define __STRING_H_CHECK_ALIGN(num, dest, src, size) ((num % size == 0) && (dest % size == 0) && (src % size == 0))

size_t strlen(const char*);
size_t strnlen(const char* str, const size_t maxlen);
void* memset(void* dest, int ch, size_t count);
void* memcpy(void* dest, const void* src, size_t count);
void* memmove(void* dest, const void* src, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);
int strcmp(const char* str1, const char* str2);
int strncmp(const char* str1, const char* str2, size_t count);
char* strtok(char* str, const char* delimiters);
char* strchr(const char* str, int character);
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t num);
char* strcat(char* destination, const char* source);
char* strdup(const char* src);
