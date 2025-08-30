#include <lib/string.h>

/**
 * @brief Locates the first occurrence of a character in a string.
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

/**
 * strrnechr - Find the last character in a string that is not equal to c
 * 
 * @param s    String to search (must not be NULL)
 * @param c    Character to avoid (cast to char internally)
 * @return     Pointer to last non-matching character, or NULL if not found
 * 
 * @note       Excludes null terminator from search
 * @example    strrnechr("hello", 'l') returns pointer to 'o'
 */
char* strrnechr(const char* s, int c)
{
	if (!s) return nullptr;

	char ch = (char)c;
	const char* end = s + strlen(s);

	// Work backwards from the end (excluding null terminator)
	while (end > s) {
		--end;
		if (*end != ch) {
			return (char*)end;
		}
	}

	return nullptr;
}
