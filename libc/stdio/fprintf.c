#include <stdio.h>

#include "./stdio.h"
#include "internal/features.h"

int __fprintf(FILE* __restrict stream, const char* __restrict format, ...)
{
	va_list args;
	va_start(args, format);
	const int ret = __vfprintf(stream, format, args);
	va_end(args);
	return ret;
}
weak_alias(__fprintf, fprintf);

void __fputc_wrapper(char c, void* stream)
{
	__fputc(c, (FILE*)stream);
}

int __vfprintf(FILE* __restrict stream,
	       const char* __restrict format,
	       va_list arg)
{
	return vfctprintf(__fputc_wrapper, stream, format, arg);
}
weak_alias(__vfprintf, vfprintf);
