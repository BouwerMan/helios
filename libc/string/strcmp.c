#include <stdint.h>
#include <string.h>
// TODO: These implementations might not be exactly what standard c libraries do. Need to investigate how null
// terminators and/or bounds are handled.

/**
 * @brief Compares two null-terminated strings lexicographically.
 *
 * Compares the strings pointed to by `str1` and `str2` character by character.
 * The comparison is done using unsigned characters and stops at the first
 * differing character or when a null terminator is encountered.
 *
 * @param   str1    Pointer to the first null-terminated string.
 * @param   str2    Pointer to the second null-terminated string.
 *
 * @return An integer less than, equal to, or greater than zero if `str1` is
 *         found to be less than, equal to, or greater than `str2` respectively.
 */
int strcmp(const char* str1, const char* str2)
{
    uint32_t i = 0;
    while (1) {
        if (str1[i] < str2[i])
            return -1;
        else if (str1[i] > str2[i])
            return 1;
        else {
            if (str1[i] == '\0') return 0;

            ++i;
        }
    }
}

/**
 * @brief Compares up to a specified number of characters of two strings.
 *
 * Compares at most `count` characters of the null-terminated strings
 * `str1` and `str2`. The comparison is done lexicographically using
 * unsigned characters and stops at the first differing character, a null
 * terminator, or after `count` characters.
 *
 * @param   str1    Pointer to the first null-terminated string.
 * @param   str2    Pointer to the second null-terminated string.
 * @param   count   Maximum number of characters to compare.
 *
 * @return An integer less than, equal to, or greater than zero if the first
 *         `count` characters of `str1` are found to be less than, equal to,
 *         or greater than those of `str2` respectively.
 */
int strncmp(const char* str1, const char* str2, size_t count)
{
    uint32_t i = 0;
    while (i < count) {
        if (str1[i] < str2[i])
            return -1;
        else if (str1[i] > str2[i])
            return 1;
        else {
            if (str1[i] == '\0') return 0;
            ++i;
        }
    }
    return 0;
}
