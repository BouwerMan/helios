#include <stdint.h>
#include <string.h>

/**
 * @brief Safely copies 32-bit words from one memory region to another, handling overlap.
 *
 * Copies `n` `uint32_t` values from the source buffer `src` to the destination buffer `dest`.
 * If the memory regions overlap, the function ensures correct behavior by choosing the
 * appropriate direction of copy: forward if `src >= dest`, backward otherwise.
 *
 * This is a specialized version of `memmove` optimized for 32-bit word-sized data.
 *
 * @param dest Pointer to the destination buffer.
 * @param src  Pointer to the source buffer.
 * @param n    Number of 32-bit elements to copy.
 * @return Pointer to the destination buffer.
 */
uint32_t* memmove32(uint32_t* dest, const uint32_t* src, size_t n)
{
	if (n == 0 || dest == src) return dest;

	uint32_t* pdest = dest;
	const uint32_t* psrc = src;

	if (psrc >= pdest) {
		for (size_t i = 0; i < n; i++) {
			pdest[i] = psrc[i];
		}
	} else if (psrc < pdest) {
		for (size_t i = n; i > 0; i--) {
			pdest[i - 1] = psrc[i - 1];
		}
	}

	return dest;
}

/**
 * @brief Safely copies bytes from one memory region to another, handling overlap.
 *
 * Copies `n` bytes from the source buffer `src` to the destination buffer `dest`.
 * The function detects overlap and adjusts the copy direction accordingly:
 * it copies forward if `src > dest`, or backward if `src < dest`, to prevent corruption.
 *
 * This function conforms to the behavior of the standard C `memmove`.
 *
 * @param dest Pointer to the destination buffer.
 * @param src  Pointer to the source buffer.
 * @param n    Number of bytes to copy.
 * @return Pointer to the destination buffer.
 */
void* memmove(void* dest, const void* src, size_t n)
{
	if (n == 0 || dest == src) return dest;

	uint8_t* pdest = (uint8_t*)dest;
	const uint8_t* psrc = (const uint8_t*)src;

	if (psrc >= pdest) {
		for (size_t i = 0; i < n; i++) {
			pdest[i] = psrc[i];
		}
	} else if (psrc < pdest) {
		for (size_t i = n; i > 0; i--) {
			pdest[i - 1] = psrc[i - 1];
		}
	}

	return dest;
}
