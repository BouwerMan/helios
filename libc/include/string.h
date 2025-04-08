#pragma once

#include <sys/cdefs.h>

#include <stddef.h>

size_t strlen(const char*);
void* memset(void*, int, size_t);
void* memcpy(void* dest, const void* src, size_t count);
void* memove(void* dest, const void* src, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);
int strcmp(const char* str1, const char* str2);
int strncmp(const char* str1, const char* str2, size_t count);
char* strtok(char* str, const char* delimiters);
char* strchr(const char* str, int character);
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t num);
char* strcat(char* destination, const char* source);
char* strdup(const char* src);
