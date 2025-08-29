#include <lib/string.h>

/**
 * @brief Locates the first occurrence of a character in a string.
 *
 * Historically the character argument was of type int, but I'm not like K&C I'm
 * significantly worse so I get to use char.
 *
 * @param   str         The null-terminated string to search.
 * @param   character   The character to find.
 *
 * @return A pointer to the first occurrence of the character,
 *         or NULL if not found.
 */
char* strchr(const char* str, int character)
{
	while (*str) {
		if (*str == character) return (char*)str;
		str++;
	}
	// Check for character == '\0' explicitly
	return (character == '\0') ? (char*)str : NULL;
}

/**
 * @brief Locate the last occurrence of a character in a string.
 *
 * @param s The input string to search.
 * @param c The character to locate, passed as an int but converted to char.
 *
 * @return A pointer to the last occurence of the character,
 *         or nullptr if not found.
 */
char* strrchr(const char* s, int c)
{
	char ch = (char)c;
	const char* last = nullptr;

	do {
		if (*s == ch) last = s;
	} while (*s++);

	return (char*)last;
}
