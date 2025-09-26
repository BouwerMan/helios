#include <lib/string.h>
#include <stdint.h>

/**
 * @brief Copies a block of memory from a source to a destination.
 *
 * This function copies `count` bytes from the memory area pointed to by `src`
 * to the memory area pointed to by `dest`. The memory areas must not overlap;
 * if they do, the behavior is undefined. Use `memmove()` if overlapping areas
 * are possible.
 *
 * @param   dest    Pointer to the destination buffer where the content is to be
 * copied.
 * @param   src     Pointer to the source of data to be copied.
 * @param   count   Number of bytes to copy.
 *
 * @return A pointer to the destination buffer dest.
 */
void* memcpy(void* restrict dest, const void* restrict src, size_t n)
{
	uint8_t* restrict pdest = (uint8_t* restrict)dest;
	const uint8_t* restrict psrc = (const uint8_t* restrict)src;

	for (size_t i = 0; i < n; i++) {
		pdest[i] = psrc[i];
	}

	return dest;
}
