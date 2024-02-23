#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/cdefs.h>

#define EOF (-1)

// printf.c

static void print(const char* data);
int printf(const char* __restrict format, ...);
int vprintf(const char* __restrict, va_list);
static void parse_num(unsigned int value, unsigned int base);

// putchar.c
int putchar(int);

// puts.c
int puts(const char*);
