#pragma once

#include <sys/cdefs.h>

#include <stddef.h>

size_t strlen(const char*);
void* memset(void*, int, size_t);
void* memcpy(void* dest, const void* src, size_t count);
int strcmp(const char* str1, const char* str2);
int strncmp(const char* str1, const char* str2, size_t count);

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
char* strtok(char* str, const char* delimiters);

/**
 * @brief Locates the first occurrence of a character in a string.
 *
 * Historically the character argument was of type int, but I'm not like K&C I'm
 * significantly worse.
 *
 * @param str The null-terminated string to search.
 * @param character The character to find.
 * @return A pointer to the first occurrence of the character,
 *         or NULL if not found.
 */
char* strchr(const char* str, char character);

char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t num);

char* strcat(char* destination, const char* source);
