#include <string.h>

/**
 * @brief Calculates the length of a null-terminated string.
 *
 * Iterates through the string pointed to by `str` until the null terminator
 * is encountered, counting the number of characters (excluding the terminator).
 *
 * Behavior is undefined if `str` is not null-terminated or if it points to
 * invalid memory.
 *
 * @param   str Pointer to the null-terminated string.
 *
 * @return The number of characters in the string, excluding the null terminator.
 */
size_t strlen(const char* str)
{
	if (!str) {
		return 0; // Handle NULL pointer gracefully
	}

	size_t len = 0;
	while (str[len])
		len++;
	return len;
}

/**
 * @brief Computes the length of a string up to a maximum length.
 *
 * This function calculates the length of the string `str`, but will not
 * exceed `maxlen` characters. It ensures safe string length computation
 * for potentially unterminated strings.
 *
 * @param str The input string (must not be NULL).
 * @param maxlen The maximum number of characters to check.
 * @return The length of the string, up to `maxlen`.
 */
size_t strnlen(const char* str, const size_t maxlen)
{
	if (!str) {
		return 0; // Handle NULL pointer gracefully
	}

	size_t len = 0;
	while (len < maxlen && str[len]) {
		len++;
	}
	return len;
}
