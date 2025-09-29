#include "errno.h"
#include "internal/features.h"
#include "internal/stdio.h"
#include "internal/unistd.h"
#include "stdio.h"

// TODO: Handle buffering properly
size_t
__fwrite(const void* buffer, size_t size, size_t count, FILE* __restrict stream)
{
	if (!stream || !buffer) {
		return 0;
	}

	ssize_t written = __write(stream->__fd, buffer, size * count);
	if (written < 0) {
		stream->__error = 1;
		return 0;
	}

	return (size_t)written / size;
}
weak_alias(__fwrite, fwrite);
