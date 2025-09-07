/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _STDIO_H
#define _STDIO_H
#pragma once

#define __need_size_t
#include <stddef.h>
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
	char* buffer;	    // The actual buffer memory
	size_t buffer_size; // Total buffer capacity
	size_t buffer_pos;  // Current position in buffer
	size_t buffer_end;  // End of valid data in buffer (for reads)

	// Underlying file descriptor
	int fd;

	// Buffering behavior
	buffer_mode_t mode;

	// Stream state flags
	unsigned int eof : 1;	   // End of file reached
	unsigned int error : 1;	   // Error occurred
	unsigned int readable : 1; // Stream supports reading
	unsigned int writable : 1; // Stream supports writing

	// Position tracking (for seekable streams)
	off_t position;
} FILE;

#ifdef __cplusplus
extern "C" {
#endif

extern FILE* stdin;
extern FILE* stdout;
extern FILE* stderr;

int fclose(FILE*);
int fflush(FILE* stream);
FILE* fopen(const char*, const char*);
size_t fread(void*, size_t, size_t, FILE*);
int fseek(FILE*, long, int);
long ftell(FILE*);
size_t fwrite(const void*, size_t, size_t, FILE*);
void setbuf(FILE*, char*);

// Pull in vfprintf
#include <printf.h>
int fprintf(FILE* stream, const char* format, ...)
	__attribute__((format(printf, 2, 3)));
int vfprintf(FILE* stream, const char* format, va_list arg)
	__attribute__((format(printf, 2, 0)));

int putchar(int c);
int fputc(int c, FILE* stream);

int fputs(const char* s, FILE* stream);
int puts(const char*);

int getchar(void);
int fgetc(FILE* stream);

void __cleanup_streams(void);
FILE* __create_stream(int fd, buffer_mode_t mode, bool readable, bool writable);
void __init_streams(void);

#ifdef __cplusplus
}
#endif

#endif /* _STDIO_H */
