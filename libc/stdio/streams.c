#include "stdio.h"
#include "stdlib.h"
#include "sys/mman.h"
#include "unistd.h"
#include <helios/errno.h>
#include <helios/mman.h>

FILE* stdin;
FILE* stdout;
FILE* stderr;

#define BUFFER_SIZE 8192

FILE* __create_stream(int fd, buffer_mode_t mode, bool readable, bool writable)
{
	FILE* stream = zalloc(sizeof(FILE));
	stream->fd = fd;

	int prot = readable ? PROT_READ : 0;
	prot |= writable ? PROT_WRITE : 0;
	void* buf = mmap(
		nullptr, BUFFER_SIZE, prot, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	stream->buffer = (char*)buf;
	stream->buffer_size = BUFFER_SIZE;
	stream->mode = mode;

	stream->readable = readable;
	stream->writable = writable;

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

int fflush(FILE* stream)
{
	if (!stream) {
		return -EINVAL;
	}
	if (!stream->writable) {
		return -EPERM;
	}

	ssize_t written = write(stream->fd, stream->buffer, stream->buffer_pos);

	if (written < 0) {
		stream->error = true;
		return EOF;
	}

	stream->buffer_pos = 0;
	return 0;
}
