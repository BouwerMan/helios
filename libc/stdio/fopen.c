#include "arch/syscall.h"
#include "errno.h"
#include "internal/features.h"
#include "internal/stdlib.h"
#include "stdio.h"

// TODO: Use internal only
#include "string.h"

// TODO: Handle modes and setup buffering
FILE* __fopen(const char* filename, const char* mode)
{
	FILE* file = __malloc(sizeof(FILE));
	if (!file) {
		return nullptr;
	}
	memset(file, 0, sizeof(FILE));

	file->__buffer_size = 4096;
	file->__buffer = __malloc(file->__buffer_size);
	if (!file->__buffer) {
		__free(file);
		return nullptr;
	}

	int fd = (int)__syscall2(SYS_OPEN, (long)filename, (long)mode);
	if (fd < 0) {
		free(file->__buffer);
		free(file);
		errno = (int)-fd;
		return nullptr;
	}

	file->__fd = fd;
	file->__buffer_pos = 0;
	file->__buffer_end = 0;

	return file;
}
weak_alias(__fopen, fopen);

int __fclose(FILE* __restrict stream)
{
	if (!stream) {
		return -1;
	}

	// TODO: Flush stream if writable
	__syscall1(SYS_CLOSE, (long)stream->__fd);
	if (stream->__buffer) {
		__free(stream->__buffer);
	}
	free(stream);

	return 0;
}
weak_alias(__fclose, fclose);
