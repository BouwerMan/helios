#include <helios/errno.h>
#include <helios/mman.h>

#include "internal/features.h"
#include "internal/stdio.h"
#include "stdlib.h"
#include "string.h"
#include "sys/mman.h"
#include "unistd.h"

#define BUFFER_SIZE 8192

FILE* stdin;
FILE* stdout;
FILE* stderr;

FILE* __create_stream(int fd, buffer_mode_t mode, bool readable, bool writable)
{
	FILE* stream = malloc(sizeof(FILE));
	if (!stream) {
		return nullptr;
	}
	memset(stream, 0, sizeof(FILE));
	stream->__fd = fd;

	int prot = readable ? PROT_READ : 0;
	prot |= writable ? PROT_WRITE : 0;
	void* buf = mmap(
		nullptr, BUFFER_SIZE, prot, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	stream->__buffer = (char*)buf;
	stream->__buffer_size = BUFFER_SIZE;
	stream->__mode = mode;

	stream->__readable = readable;
	stream->__writable = writable;

	return stream;
}

void __init_streams(void)
{
	stdin = __create_stream(0, STREAM_LINEBUFFERED, true, false);  // stdin
	stdout = __create_stream(1, STREAM_LINEBUFFERED, false, true); // stdout
	stderr = __create_stream(2, STREAM_UNBUFFERED, false, true);   // stderr
}

void __cleanup_streams(void)
{
	fflush(stdout);
	fflush(stderr);
}

int __fflush(FILE* stream)
{
	if (!stream) {
		return -EINVAL;
	}
	if (!stream->__writable) {
		return -EPERM;
	}

	ssize_t written =
		write(stream->__fd, stream->__buffer, stream->__buffer_pos);

	if (written < 0) {
		stream->__error = true;
		return EOF;
	}

	stream->__buffer_pos = 0;
	return 0;
}
weak_alias(__fflush, fflush);
