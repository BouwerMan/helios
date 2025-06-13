/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _STDIO_H
#define _STDIO_H
#pragma once

#define __need_size_t
#include <stddef.h>

#define EOF	  (-1)
#define EOVERFLOW -75
#define SEEK_SET  0
typedef struct {
	int unused;
} FILE;

#ifdef __cplusplus
extern "C" {
#endif

extern FILE* stderr;
#define stderr stderr

int fclose(FILE*);
int fflush(FILE*);
FILE* fopen(const char*, const char*);
int fprintf(FILE*, const char*, ...);
size_t fread(void*, size_t, size_t, FILE*);
int fseek(FILE*, long, int);
long ftell(FILE*);
size_t fwrite(const void*, size_t, size_t, FILE*);
void setbuf(FILE*, char*);

// Pull in vfprintf
#include <printf.h>
//int vfprintf(FILE*, const char*, va_list);

int putchar(int);
int puts(const char*);

#ifdef __cplusplus
}
#endif

#endif /* _STDIO_H */
