#include <stdlib.h>
#include <string.h>

/**
 * @brief Duplicates a string by allocating memory and copying its content.
 *
 * This function creates a duplicate of the input string by allocating
 * sufficient memory and copying the string, including the null terminator.
 *
 * @param src Pointer to the null-terminated string to duplicate.
 *            If `src` is NULL, the function returns NULL.
 *
 * @return Pointer to the newly allocated string containing the duplicate.
 *         Returns NULL if memory allocation fails or if `src` is NULL.
 *
 * @note The caller is responsible for freeing the memory allocated for
 *       the duplicated string using `free()` or an equivalent function.
 */
char* strdup(const char* src)
{
	if (src == nullptr) return nullptr;

	size_t len = strlen(src);
	char* new = malloc(len + 1);
	if (new == nullptr) return nullptr;

	return strcpy(new, src);
}
