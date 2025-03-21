#include <stdbool.h>
#include <string.h>

/**
 * @brief Splits a string into tokens using specified delimiters.
 *
 * This function tokenizes a string by replacing delimiter characters with
 * null terminators (`\0`). On the first call, provide the string to be
 * tokenized. On subsequent calls, pass `NULL` to continue tokenizing the
 * same string.
 *
 * @param   str         The string to tokenize (or NULL to continue tokenizing the last string).
 * @param   delimiters  A string containing delimiter characters.
 *
 * @return A pointer to the next token, or NULL if no more tokens are found.
 *
 * @note This function modifies the original string by inserting `\0` characters
 *       in place of delimiters. It is not thread-safe due to its use of a
 *       static internal pointer.
 *
 * Example usage:
 * @code
 * char str[] = "hello,world,test";
 * char *token = strtok(str, ",");
 * while (token) {
 *     printf("%s\n", token);
 *     token = strtok(NULL, ",");
 * }
 * @endcode
 */
char* strtok(char* str, const char* delimiters)
{
    static char* trunc; // String to truncate
    if (str) {
        trunc = str; // if string exists, we set trunc, otherwise we use
                     // previous trunc value
    } else if (trunc == NULL) {
        return NULL;
    }

    char* token_start = NULL;
    // Skip initial delimiters
    while (*trunc && strchr(delimiters, *trunc))
        trunc++;

    if (*trunc == '\0') return NULL;
    token_start = trunc;
    while (*trunc) {
        if (strchr(delimiters, *trunc)) {
            *trunc++ = '\0';
            return token_start;
        }
        trunc++;
    }
    if (*trunc == '\0') {
        trunc = NULL;
        return token_start;
    }

    return NULL;
}
