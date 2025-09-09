#include <helios/errno.h>

#include "arch/syscall.h"
#include "internal/features.h"
#include "printf.h"
#include "stdint.h"
#include "stdio.h"

int __putchar(int ic)
{
	char c = (char)ic;
	__syscall3(SYS_WRITE, 1, (long)&c, 1);
	return ic;
}
weak_alias(__putchar, putchar);

int __fputc(int c, FILE* stream)
{
	if (!stream) {
		return -EINVAL;
	}
	if (!stream->__writable) {
		return -EPERM;
	}

	stream->__buffer[stream->__buffer_pos++] = (char)c;
	bool should_flush = false;

	switch (stream->__mode) {
	case STREAM_UNBUFFERED: should_flush = true; break;
	case STREAM_LINEBUFFERED:
		should_flush = (c == '\n') ||
			       (stream->__buffer_pos >= stream->__buffer_size);
		break;
	case STREAM_FULLYBUFFERED:
		should_flush = (stream->__buffer_pos >= stream->__buffer_size);
		break;
	}

	if (should_flush) {
		return fflush(stream);
	}

	return c;
}
weak_alias(__fputc, fputc);

// Needed for printf lib
void putchar_(char c)
{
	__fputc(c, stdout);
}
