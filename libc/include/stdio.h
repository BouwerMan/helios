/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _STDIO_H
#define _STDIO_H
#pragma once

#define __need_size_t
#include <stddef.h>

#include <features.h>
#include <sys/types.h>

#define EOF	 (-1)
#define SEEK_SET 0

typedef enum {
	STREAM_UNBUFFERED,
	STREAM_LINEBUFFERED,
	STREAM_FULLYBUFFERED,
} buffer_mode_t;

typedef struct __file_stream {
	// Buffer management
	char* __buffer;	      // The actual buffer memory
	size_t __buffer_size; // Total buffer capacity
	size_t __buffer_pos;  // Current position in buffer
	size_t __buffer_end;  // End of valid data in buffer (for reads)

	// Underlying file descriptor
	int __fd;

	// Buffering behavior
	buffer_mode_t __mode;

	// Stream state flags
	unsigned int __eof : 1;	     // End of file reached
	unsigned int __error : 1;    // Error occurred
	unsigned int __readable : 1; // Stream supports reading
	unsigned int __writable : 1; // Stream supports writing

	// Position tracking (for seekable streams)
	off_t __position;
} FILE;

#ifdef __cplusplus
extern "C" {
#endif

extern FILE* stdin;
extern FILE* stdout;
extern FILE* stderr;

int fclose(FILE*);
int fflush(FILE* __stream) __nothrow;
FILE* fopen(const char*, const char*);
size_t fread(void*, size_t, size_t, FILE*);
int fseek(FILE*, long, int);
long ftell(FILE*);
size_t fwrite(const void*, size_t, size_t, FILE*);
void setbuf(FILE*, char*);
void perror(const char* __s) __nothrow;

// Pull in vfprintf
#include <printf.h>
int fprintf(FILE* __restrict __stream, const char* __restrict __format, ...)
	__attribute__((format(printf, 2, 3))) __nothrow;
int vfprintf(FILE* __restrict __stream,
	     const char* __restrict __format,
	     va_list __arg) __attribute__((format(printf, 2, 0))) __nothrow;

int putchar(int __c) __nothrow;
int fputc(int __c, FILE* __stream) __nothrow;

int fputs(const char* __restrict __s, FILE* __restrict __stream) __nothrow;
int puts(const char* __s) __nothrow;

int getchar(void);
int fgetc(FILE* stream);

#ifdef __cplusplus
}
#endif

#endif /* _STDIO_H */
