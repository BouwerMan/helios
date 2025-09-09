#pragma once

#include <stdio.h>

int __fputc(int c, FILE* stream);
int __putchar(int ic);

int __fprintf(FILE* __restrict stream, const char* __restrict format, ...);
int __vfprintf(FILE* __restrict stream,
	       const char* __restrict format,
	       va_list arg);
