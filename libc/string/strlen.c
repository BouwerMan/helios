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
    size_t len = 0;
    while (str[len])
        len++;
    return len;
}
