#include <stdint.h>
#include <string.h>
#include <util/log.h>

/**
 * @brief Moves 64-bit values between overlapping memory regions.
 *
 * Copies `count` 64-bit words from `src` to `dest`, correctly handling overlap.
 *
 * @param dest  Destination buffer.
 * @param src   Source buffer.
 * @param count Number of 64-bit elements to move.
 * @return Pointer to the destination buffer.
 */
static uint64_t* memmove64(uint64_t* dest, const uint64_t* src, size_t count)
{
	if (src < dest) {
		// backwards copy
		for (size_t i = count; i > 0; i--) {
			dest[i - 1] = src[i - 1];
		}
	} else {
		// forwards copy
		for (size_t i = 0; i < count; i++) {
			dest[i] = src[i];
		}
	}

	return dest;
}

/**
 * @brief Moves 32-bit values between overlapping memory regions.
 *
 * @param dest  Destination buffer.
 * @param src   Source buffer.
 * @param count Number of 32-bit elements to move.
 * @return Pointer to the destination buffer.
 */
static uint32_t* memmove32(uint32_t* dest, const uint32_t* src, size_t count)
{
	if (src < dest) {
		// backwards copy
		for (size_t i = count; i > 0; i--) {
			dest[i - 1] = src[i - 1];
		}
	} else {
		// forwards copy
		for (size_t i = 0; i < count; i++) {
			dest[i] = src[i];
		}
	}

	return dest;
}

/**
 * @brief Moves 16-bit values between overlapping memory regions.
 *
 * @param dest  Destination buffer.
 * @param src   Source buffer.
 * @param count Number of 16-bit elements to move.
 * @return Pointer to the destination buffer.
 */
static uint16_t* memmove16(uint16_t* dest, const uint16_t* src, size_t count)
{
	if (src < dest) {
		// backwards copy
		for (size_t i = count; i > 0; i--) {
			dest[i - 1] = src[i - 1];
		}
	} else {
		// forwards copy
		for (size_t i = 0; i < count; i++) {
			dest[i] = src[i];
		}
	}

	return dest;
}

/**
 * @brief Moves 8-bit values between overlapping memory regions.
 *
 * @param dest  Destination buffer.
 * @param src   Source buffer.
 * @param count Number of bytes to move.
 * @return Pointer to the destination buffer.
 */
static uint8_t* memmove8(uint8_t* dest, const uint8_t* src, size_t count)
{
	if (src < dest) {
		// backwards copy
		for (size_t i = count; i > 0; i--) {
			dest[i - 1] = src[i - 1];
		}
	} else {
		// forwards copy
		for (size_t i = 0; i < count; i++) {
			dest[i] = src[i];
		}
	}

	return dest;
}

/// Checks the alignment of dest and src while making sure num can be evenly divisible
#define CHECK_ALIGN(num, dest, src, size) ((num % size == 0) && (dest % size == 0) && (src % size == 0))

/**
 * @brief Safely copies bytes between potentially overlapping memory regions.
 *
 * Copies `n` bytes from `src` to `dest`, handling overlap by adjusting the copy direction.
 * The function attempts to use the widest possible aligned transfer for optimal performance.
 *
 * @param dest Pointer to the destination buffer.
 * @param src  Pointer to the source buffer.
 * @param n    Number of bytes to copy.
 * @return Pointer to the destination buffer.
 */

void* memmove(void* dest, const void* src, size_t n)
{
	if (n == 0 || dest == src) return dest;

	uintptr_t d = (uintptr_t)dest;
	uintptr_t s = (uintptr_t)src;

	// We check that n is divisible by the larger integer type.
	// We also assure that d and s are aligned properly.
	if (CHECK_ALIGN(n, d, s, sizeof(uint64_t))) {
		return (void*)memmove64(dest, src, n / sizeof(uint64_t));
	} else if (CHECK_ALIGN(n, d, s, sizeof(uint32_t))) {
		return (void*)memmove32(dest, src, n / sizeof(uint32_t));
	} else if (CHECK_ALIGN(n, d, s, sizeof(uint16_t))) {
		return (void*)memmove16(dest, src, n / sizeof(uint16_t));
	} else {
		return (void*)memmove8(dest, src, n);
	}
}
