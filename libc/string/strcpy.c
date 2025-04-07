#include <stdbool.h>
#include <string.h>

/**
 * @brief Copies a null-terminated string to a destination buffer.
 *
 * Copies the string pointed to by `src` (including the null terminator)
 * to the buffer pointed to by `dest`. The destination buffer must be large
 * enough to hold the entire string.
 *
 * Behavior is undefined if the memory regions overlap. Use with caution,
 * especially in low-level code where memory safety must be ensured.
 *
 * @param   dest    Pointer to the destination buffer.
 * @param   src     Pointer to the null-terminated source string.
 *
 * @return A pointer to the destination buffer `dest`.
 */
char* strcpy(char* dest, const char* src)
{
    char* original_dest = dest; // Maintains start of dest
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0'; // manually copy null termination
    return original_dest;
}

/**
 * @brief Copies up to a specified number of characters from one string to another.
 *
 * Copies at most `num` characters from the null-terminated string `src` to
 * `dest`. If `src` is shorter than `num` characters, the remainder of `dest`
 * is padded with null bytes. If `src` is longer than or equal to `num`, no
 * null terminator is added to `dest`.
 *
 * The destination buffer must be large enough to hold at least `num` characters.
 * This function does not guarantee null termination unless `src` is shorter than `num`.
 *
 * @param   dest    Pointer to the destination buffer.
 * @param   src     Pointer to the null-terminated source string.
 * @param   num     Maximum number of characters to copy.
 *
 * @return A pointer to the destination buffer `dest`.
 */
char* strncpy(char* dest, const char* src, size_t num)
{
    size_t i = 0;

    // Copy characters until we hit num or find '\0'
    while (i < num && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }

    // Fill the rest with '\0'
    while (i < num) {
        dest[i] = '\0';
        i++;
    }

    return dest;
}
