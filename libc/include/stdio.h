#pragma once

#include <kernel/screen.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/cdefs.h>

#define EOF	  (-1)
#define EOVERFLOW -75

// printf.c

// static void print(const char* data);

// __attribute__((format(__printf__, 1, 2))) int printf(const char* __restrict format, ...);

// int vprintf(const char* __restrict, va_list);
// int sprintf(char* str, const char* __restrict format, ...);
// int snprintf(char* str, size_t buf_size, const char* __restrict format, ...);

// putchar.c
int putchar(int);

// puts.c
int puts(const char*);
