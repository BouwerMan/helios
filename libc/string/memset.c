#include <stdint.h>
#include <string.h>
#include <util/log.h>

static uint64_t* memset64(uint64_t* dst, uint64_t value, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		dst[i] = value;
	}
	return dst;
}

static uint32_t* memset32(uint32_t* dst, uint32_t value, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		dst[i] = value;
	}
	return dst;
}

static uint16_t* memset16(uint16_t* dst, uint16_t value, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		dst[i] = value;
	}
	return dst;
}

static uint8_t* memset8(uint8_t* dst, uint8_t value, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		dst[i] = value;
	}
	return dst;
}

// Ensures n can be divided by size and ensures src is properly aligned
#define CHECK_ALIGN(d, n, size) ((d % size == 0) && (n % size == 0))

void* memset(void* dest, int ch, size_t count)
{
	unsigned char c = (unsigned char)ch;
	uintptr_t d = (uintptr_t)dest;

	// If count is small we just use memset 8 and call it a day
	if (count < 32) return memset8(dest, c, count);

	if (CHECK_ALIGN(d, count, sizeof(uint64_t))) {
		uint64_t val = 0x0101010101010101ULL * c;
		return memset64(dest, val, count / sizeof(uint64_t));
	} else if (CHECK_ALIGN(d, count, sizeof(uint32_t))) {
		uint32_t val = 0x01010101UL * c;
		return memset32(dest, val, count / sizeof(uint32_t));
	} else if (CHECK_ALIGN(d, count, sizeof(uint16_t))) {
		uint16_t val = 0x0101U * c;
		return memset16(dest, val, count / sizeof(uint16_t));
	} else {
		unsigned char val = c;
		return memset8(dest, val, count);
	}
}
