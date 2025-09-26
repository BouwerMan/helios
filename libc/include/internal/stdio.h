#pragma once

#include <stdio.h>

void __cleanup_streams(void);
FILE* __create_stream(int fd, buffer_mode_t mode, bool readable, bool writable);
void __init_streams(void);
int __fflush(FILE* stream);

int __fputc(int c, FILE* stream);
int __putchar(int ic);
int __puts(const char* str);
int __fputs(const char* __restrict s, FILE* stream);

int __fprintf(FILE* __restrict stream, const char* __restrict format, ...);
int __vfprintf(FILE* __restrict stream,
	       const char* __restrict format,
	       va_list arg);

int __getchar(void);
int __fgetc(FILE* stream);

void __perror(const char* s);
