#include <drivers/serial.h>
#include <printf.h>
#include <string.h>
#include <util/log.h>

void log_output(const char* msg)
{
	printf("%s", msg); // Console / screen output
#if ENABLE_SERIAL_LOGGING
	write_serial_string(msg); // Custom serial output
#endif
}

void log_long_message(const char* tag, const char* file, int line,
		      const char* func, const char* msg)
{
	char buf[LOG_BUFFER_SIZE];
	size_t tag_len = snprintf(buf, sizeof(buf), "[%s] %s:%d:%s(): ", tag,
				  file, line, func);

	const char* p = msg;
	while (*p) {
		// leave space for newline and null
		size_t chunk_size = LOG_BUFFER_SIZE - tag_len - 2;
		size_t len = strnlen(p, chunk_size);

		memcpy(buf + tag_len, p, len);
		buf[tag_len + len] = '\n';
		buf[tag_len + len + 1] = '\0';

		log_output(buf);
		p += len;
	}
}
