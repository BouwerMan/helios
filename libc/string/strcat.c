#include <string.h>

/**
 * @brief Appends a string to the end of another string.
 *
 * Concatenates the null-terminated string `src` to the end of the
 * null-terminated string `dest`. The first character of `src`
 * overwrites the null terminator at the end of `dest`. The resulting
 * string in `dest` is also null-terminated.
 *
 * The caller must ensure that `dest` has enough space to hold the
 * resulting concatenated string, including the null terminator.
 *
 * @param   dest    Pointer to the destination string buffer.
 * @param   src     Pointer to the source string to append.
 *
 * @return A pointer to the destination string `dest`.
 */
char* strcat(char* dest, const char* src)
{
    size_t dest_len = strlen(dest); // Finding null terminator of dest
    return strcpy(dest + dest_len, src);
}
