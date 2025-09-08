#ifndef _STDLIB_H
#define _STDLIB_H
#pragma once

#define __need_size_t
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Convert ASCII string to integer
 * 
 * Parses the string pointed to by nptr, interpreting its content as an integral
 * number according to the C standard. The function discards any whitespace
 * characters until the first non-whitespace character is found. Then, starting
 * from this character, takes an optional plus or minus sign followed by as many
 * numerical digits as possible, and interprets them as a numerical value.
 * 
 * @param nptr Pointer to the null-terminated byte string to be interpreted
 * 
 * @return The converted integral number as an int value. If no valid conversion
 *         could be performed, it returns zero. If the value is out of the range
 *         of representable values by an int, the behavior is undefined.
 * 
 * @note This function does not detect overflow conditions. For applications
 *       requiring overflow detection, consider using strtol() instead.
 * 
 * @note The function stops reading the string at the first character that
 *       cannot be part of an integral number. The rest of the string is ignored.
 * 
 * @example
 *   atoi("123") returns 123
 *   atoi("  -456") returns -456
 *   atoi("123abc") returns 123
 *   atoi("abc") returns 0
 */
int atoi(const char* nptr);

__attribute__((__noreturn__)) void abort(void);
int atexit(void (*func)(void));
char* getenv(const char* name);
[[noreturn]] void exit(int status);

#include <liballoc.h>
// void free(void* ptr);
// void* malloc(size_t size);

#ifdef __cplusplus
}
#endif

#endif /* _STDLIB_H */
