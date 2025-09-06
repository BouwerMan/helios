#include "stdio.h"
#include "unistd.h"

int getchar(void)
{
	return fgetc(stdin);
}

int fgetc(FILE* stream)
{
	char c;
	ssize_t r = read(stream->fd, &c, 1);
	return c;
}
