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
 * @param str The string to tokenize (or NULL to continue tokenizing the last
 * string).
 * @param delimiters A string containing delimiter characters.
 *
 * @returns A pointer to the next token, or NULL if no more tokens are found.
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
