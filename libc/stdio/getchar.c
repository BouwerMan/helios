#include "errno.h"
#include "internal/features.h"
#include "internal/stdio.h"
#include "unistd.h"

int __getchar(void)
{
	return __fgetc(stdin);
}
weak_alias(__getchar, getchar);

int __fgetc(FILE* stream)
{
	char c;
	ssize_t r = read(stream->__fd, &c, 1);
	if (r < 0) {
		stream->__error = 1;
		errno = (int)-r;
		return -1;
	} else if (r == 0) {
		stream->__eof = 1;
		return -1;
	}
	return c;
}
weak_alias(__fgetc, fgetc);
