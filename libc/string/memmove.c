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
static uint64_t* memmove64(uint64_t* restrict dest, const uint64_t* restrict src, size_t count)
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
static uint32_t* memmove32(uint32_t* restrict dest, const uint32_t* restrict src, size_t count)
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
static uint16_t* memmove16(uint16_t* restrict dest, const uint16_t* restrict src, size_t count)
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
static uint8_t* memmove8(uint8_t* restrict dest, const uint8_t* restrict src, size_t count)
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

#if defined(__x86_64__) || defined(_M_X64)
// Ensures n can be divided by size, on x86_64 we can just eat the performance degradation from misaligned pointers.
#define CHECK_ALIGN(num, dest, src, size) ((num % size == 0))
#else
// On other architectures, unaligned pointer access might not be allowed
#define CHECK_ALIGN(num, dest, src, size) ((num % size == 0) && (dest % size == 0) && (src % size == 0))
#endif

#define SMALL_MOVE_THRESHOLD 1024

/**
 * @brief Choose the widest aligned copy for small moves.
 *
 * Dispatches to 64/32/16/8‐bit element movers based on alignment
 * and divisibility of `n`.
 *
 * @param dest  Destination buffer (may overlap `src`).
 * @param src   Source buffer (may overlap `dest`).
 * @param n     Number of bytes to move.
 * @return      Pointer to `dest`.
 */
static void* small_memmove(void* restrict dest, const void* restrict src, size_t n)
{
	// If count is small we just use memmove 8 and call it a day
	if (n <= 32) return (void*)memmove8(dest, src, n);

	// We check that n is divisible by the larger integer type.
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

/**
 * @brief Fast forward-copy path using the CPU string engine.
 *
 * Aligns `dest` to an 8‐byte boundary, then issues a
 * REP MOVSQ loop for the bulk and REP MOVSB for the leftover bytes.
 *
 * @param dest  Destination buffer (`dest` ≤ `src` + `n`).
 * @param src   Source buffer.
 * @param n     Number of bytes to move.
 * @return      Pointer to original `dest`.
 */
static void* forward_move(void* restrict dest, const void* restrict src, size_t n)
{
	uint8_t* b = dest;
	const uint8_t* s = src;

	// Phase 1: peel off head bytes until b is 8-aligned
	size_t head_bytes = ((uintptr_t)b) & 7;
	if (head_bytes) {
		size_t peel = 8 - head_bytes;
		if (peel > n) peel = n;

		for (size_t i = 0; i < peel; i++) {
			b[i] = s[i];
		}

		b += peel;
		s += peel;
		n -= peel;
	}

	// Phase 2: Copy 8-byte chunks
	register uint64_t* d64 __asm__("rdi") = (uint64_t*)b;
	register const uint64_t* s64 __asm__("rsi") = (const uint64_t*)s;
	register size_t cnt __asm__("rcx") = n / 8;

	__asm__ volatile("rep movsq" : "+D"(d64), "+S"(s64), "+c"(cnt)::"memory");

	// Phase 3: Copy remaining bytes
	cnt = n % 8;

	__asm__ volatile("rep movsb" : "+D"(d64), "+S"(s64), "+c"(cnt)::"memory");

	return dest;
}

/**
 * @brief Backward-copy path for overlapping dest>src cases.
 *
 * Peels off the final few bytes so that (d+1) is 8-aligned,
 * then does an 8× unrolled QWORD loop in C, and finishes
 * any trailing bytes one by one.
 *
 * @param dest  Pointer to last byte of destination (dest_end).
 * @param src   Pointer to last byte of source (src_end).
 * @param n     Number of bytes to move.
 * @return      Pointer to original start of destination.
 */
static void* backward_move(void* restrict dest, const void* restrict src, size_t n)
{
	// NOTE: Because we are moving backwards, I'm going to be lazy and just use the c only style.
	// The forward path is usually the hot path anyways
	uint8_t* d = dest;
	const uint8_t* s = src;

	// Phase 1: peel off tail bytes so (b+1) is 8-aligned
	size_t peel = ((uintptr_t)(d + 1)) & 7;
	if (peel > n) peel = n;
	for (size_t i = 0; i < peel; i++) {
		*d-- = *s--;
	}
	n -= peel;

	// Phase 2: Fill 8-byte chunks
	// d+1 is aligned, so (uint64_t*)(d+1) is the pointer *one past* the last word
	uint64_t* p64 = ((uint64_t*)(d + 1)) - 1;
	const uint64_t* s64 = ((const uint64_t*)(s + 1)) - 1;
	// Total 8 byte chunks
	size_t num_chunks = n / 8;
	// Groups of x that we do at a time where x is the number of unrolls (8)
	size_t num_blocks = num_chunks / 8;

	for (size_t i = 0; i < num_blocks; i++) {
		p64[0] = s64[0];
		p64[-1] = s64[-1];
		p64[-2] = s64[-2];
		p64[-3] = s64[-3];
		p64[-4] = s64[-4];
		p64[-5] = s64[-5];
		p64[-6] = s64[-6];
		p64[-7] = s64[-7];
		p64 -= 8;
		s64 -= 8;
	}

	// Handle the leftover (num_chunks % 8) 8-byte stores
	for (size_t i = 0; i < (num_chunks % 8); i++) {
		*p64-- = *s64--;
	}

	d = (uint8_t*)p64;
	s = (uint8_t*)s64;

	// Phase 3: Handle remaining bytes
	size_t tail_bytes = n % 8;
	for (size_t i = 0; i < tail_bytes; i++) {
		*d-- = *s--;
	}

	return (uint8_t*)(d + 1);
}

/**
 * @brief High-performance memmove with small/forward/backward paths.
 *
 * - If @p n ≤ `SMALL_MOVE_THRESHOLD`, calls @ref small_memmove for
 *   a simple element‐width dispatch (beats REP for small sizes).
 * - Otherwise, computes whether regions overlap (dest > src && dest < src+n):
 *   - Non-overlapping or forward‐safe: calls @ref forward_move, which
 *     uses the CPU’s REP MOVSQ/MOVSB string engine for max throughput.
 *   - Overlapping backwards: calls @ref backward_move, which does
 *     a C‐level peel/ unrolled‐QWORD/ tail sequence (overlap is rare).
 *
 * @param dest  Destination buffer (may overlap @p src).
 * @param src   Source buffer (may overlap @p dest).
 * @param n     Number of bytes to copy.
 * @return      Pointer to @p dest.
 */
void* memmove(void* restrict dest, const void* restrict src, size_t n)
{
	if (n == 0 || dest == src) return dest;

	if (n <= SMALL_MOVE_THRESHOLD) {
		// This function generally performs better at small moves
		return small_memmove(dest, src, n);
	}

	uintptr_t d = (uintptr_t)dest;
	uintptr_t s = (uintptr_t)src;
	bool overlap = (d > s) && ((d - s) < n);

	// TODO: May need to double check that these different paths work safely
	// My testbench stuff was kinda funky
	if (__builtin_expect(!overlap, 1)) {
		// forward (likely)
		return forward_move(dest, src, n);
	} else {
		// backwards (unlikely), pass end pointers to backward_move
		return backward_move((void*)(d + (n - 1)), (void*)(s + (n - 1)), n);
	}

	return dest;
}
