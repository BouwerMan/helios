#include <lib/string.h>
#include <stdint.h>

// TODO: rework all of this to use mainly rep stosb
//
// These will be aliased in memset.c
void* memset64(uint64_t* s, uint64_t v, size_t n);
void* memset32(uint32_t* s, uint32_t v, size_t n);
void* memset16(uint16_t* s, uint16_t v, size_t n);
void* memset8(uint8_t* s, uint8_t v, size_t n);

#define __HAVE_ARCH_MEMSET8
static inline void* __arch_memset8(uint8_t* s, uint8_t v, size_t n)
{
	uint8_t* s0 = s;
	__asm__ volatile("rep stosb" : "+D"(s), "+c"(n) : "a"(v) : "memory");
	return s0;
}

#define __HAVE_ARCH_MEMSET16
static inline void* __arch_memset16(uint16_t* s, uint16_t v, size_t n)
{
	uint16_t* s0 = s;
	__asm__ volatile("rep stosw" : "+D"(s), "+c"(n) : "a"(v) : "memory");
	return s0;
}

#define __HAVE_ARCH_MEMSET32
static inline void* __arch_memset32(uint32_t* s, uint32_t v, size_t n)
{
	uint32_t* s0 = s;
	__asm__ volatile("rep stosl" : "+D"(s), "+c"(n) : "a"(v) : "memory");
	return s0;
}

#define __HAVE_ARCH_MEMSET64
static inline void* __arch_memset64(uint64_t* s, uint64_t v, size_t n)
{
	uint64_t* s0 = s;
	__asm__ volatile("rep stosq" : "+D"(s), "+c"(n) : "a"(v) : "memory");
	return s0;
}

[[maybe_unused]]
static uint64_t* __memset64(uint64_t* restrict dst,
			    uint64_t value,
			    size_t count)
{
	for (size_t i = 0; i < count; i++) {
		dst[i] = value;
	}
	return dst;
}

[[maybe_unused]]
static uint32_t* __memset32(uint32_t* restrict dst,
			    uint32_t value,
			    size_t count)
{
	for (size_t i = 0; i < count; i++) {
		dst[i] = value;
	}
	return dst;
}

[[maybe_unused]]
static uint16_t* __memset16(uint16_t* restrict dst,
			    uint16_t value,
			    size_t count)
{
	for (size_t i = 0; i < count; i++) {
		dst[i] = value;
	}
	return dst;
}

[[maybe_unused]]
static uint8_t* __memset8(uint8_t* restrict dst, uint8_t value, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		dst[i] = value;
	}
	return dst;
}

#if defined(__x86_64__) || defined(_M_X64)
// Ensures n can be divided by size, on x86_64 we can just eat the performance degradation from misaligned pointers.
#define CHECK_ALIGN(d, n, size) ((void)(d), ((n) % (size) == 0))
#else
// On other architectures, unaligned pointer access might not be allowed
#define CHECK_ALIGN(d, n, size) ((d % size == 0) && (n % size == 0))
#endif

#define SMALL_MOVE_THRESHOLD 1024

static void* small_memset(void* restrict dest, int ch, size_t count)
{
	unsigned char c = (unsigned char)ch;
	uintptr_t d = (uintptr_t)dest;

	// If count is small we just use memset 8 and call it a day
	if (count < 128) return __memset8(dest, c, count);

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

void* memset(void* restrict dest, int ch, size_t count)
{
	if (count <= SMALL_MOVE_THRESHOLD) {
		// I've found that my old memset works way better for small sizes
		return small_memset(dest, ch, count);
	}

	uint8_t* b = dest;
	unsigned char c = (unsigned char)ch;

	// Phase 1: Align to 8-byte boundary
	size_t head_bytes = ((uintptr_t)b) & 7;
	if (head_bytes) {
		size_t bytes_to_fill = 8 - head_bytes;
		size_t head_fill = (bytes_to_fill < count) ? (bytes_to_fill) :
							     count;
		for (size_t i = 0; i < head_fill; i++) {
			b[i] = c;
		}
		b += head_fill;
		count -= head_fill;
	}

	// phase 2: rep stosq / stosb
	{
		// prepare registers for inline asm
		register uint64_t* d64 __asm__("rdi") = (uint64_t*)b;
		register size_t cnt64 __asm__("rcx") = count / 8;
		register uint64_t pattern __asm__("rax") =
			0x0101010101010101ULL * c;

		__asm__ volatile("rep stosq"
				 : "+D"(d64), "+c"(cnt64)
				 : "a"(pattern)
				 : "memory");

		// leftover bytes
		register size_t cnt8 __asm__("rcx") = count & 7;
		__asm__ volatile("rep stosb"
				 : "+D"(d64), "+c"(cnt8)
				 :
				 : "memory");
	}

	return dest;
}

#ifdef __HAVE_ARCH_MEMSET8
extern void* memset8(uint8_t* s, uint8_t v, size_t n)
	__attribute__((alias("__arch_memset8")));
#else
extern void* memset8(uint8_t* s, uint8_t v, size_t n)
	__attribute__((alias("__memset8")));
#endif

#ifdef __HAVE_ARCH_MEMSET16
extern void* memset16(uint16_t* s, uint16_t v, size_t n)
	__attribute__((alias("__arch_memset16")));
#else
extern void* memset16(uint16_t* s, uint16_t v, size_t n)
	__attribute__((alias("__memset16")));
#endif

#ifdef __HAVE_ARCH_MEMSET32
extern void* memset32(uint32_t* s, uint32_t v, size_t n)
	__attribute__((alias("__arch_memset32")));
#else
extern void* memset32(uint32_t* s, uint32_t v, size_t n)
	__attribute__((alias("__memset32")));
#endif

#ifdef __HAVE_ARCH_MEMSET64
extern void* memset64(uint64_t* s, uint64_t v, size_t n)
	__attribute__((alias("__arch_memset64")));
#else
extern void* memset64(uint64_t* s, uint64_t v, size_t n)
	__attribute__((alias("__memset64")));
#endif
