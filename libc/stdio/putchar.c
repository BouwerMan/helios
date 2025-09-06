#include <helios/errno.h>

#include "arch/syscall.h"
#include "printf.h"
#include "stdint.h"
#include "stdio.h"

int putchar(int ic)
{
	char c = (char)ic;
	// TODO: Implement stdio and the write system call.
	__syscall3(SYS_WRITE, 1, (long)&c, 1);
	return ic;
}

int fputc(int c, FILE* stream)
{
	if (!stream) {
		return -EINVAL;
	}
	if (!stream->writable) {
		return -EPERM;
	}

	stream->buffer[stream->buffer_pos++] = (char)c;
	bool should_flush = false;

	switch (stream->mode) {
	case STREAM_UNBUFFERED:
		should_flush = true;
		break;
	case STREAM_LINEBUFFERED:
		should_flush = (c == '\n') ||
			       (stream->buffer_pos >= stream->buffer_size);
		break;
	case STREAM_FULLYBUFFERED:
		should_flush = (stream->buffer_pos >= stream->buffer_size);
		break;
	}

	if (should_flush) {
		return fflush(stream);
	}

	return c;
}

// Needed for printf lib
void putchar_(char c)
{
	fputc(c, stdout);
}
