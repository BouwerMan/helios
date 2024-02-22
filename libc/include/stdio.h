#pragma once

#include <stdarg.h>
#include <sys/cdefs.h>

#define EOF (-1)

int printf(const char* __restrict format, ...);
int nprintf(const char* __restrict format, ...);
int nvprintf(const char* __restrict, va_list);
int putchar(int);
int puts(const char*);
