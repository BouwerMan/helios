#include "internal/features.h"
#include "stdio.h"
#include "unistd.h"

int __getchar(void)
{
	return fgetc(stdin);
}
weak_alias(__getchar, getchar);

int __fgetc(FILE* stream)
{
	char c;
	ssize_t r = read(stream->__fd, &c, 1);
	return c;
}
weak_alias(__fgetc, fgetc);
