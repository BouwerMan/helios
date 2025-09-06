#include "stdio.h"

int fprintf(FILE* stream, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	const int ret = vfprintf(stream, format, args);
	va_end(args);
	return ret;
}

void __fputc_wrapper(char c, void* stream)
{
	fputc(c, (FILE*)stream);
}

int vfprintf(FILE* stream, const char* format, va_list arg)
{
	return vfctprintf(__fputc_wrapper, stream, format, arg);
}
