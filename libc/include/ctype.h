/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _CTYPE_H
#define _CTYPE_H
#pragma once

#include <features.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Convert a character to uppercase
 * @param c Character to convert
 * @return Uppercase equivalent if c is lowercase, otherwise c unchanged
 */
int toupper(int c) __attribute_const __no_throw;

/**
 * @brief Convert a character to lowercase
 * @param c Character to convert
 * @return Lowercase equivalent if c is uppercase, otherwise c unchanged
 */
int tolower(int c) __attribute_const __no_throw;

/**
 * @brief Test if character is alphabetic
 * @param c Character to test
 * @return Non-zero if c is a letter (a-z, A-Z), zero otherwise
 */
int isalpha(int c) __attribute_const __no_throw;

/**
 * @brief Test if character is a decimal digit
 * @param c Character to test
 * @return Non-zero if c is a digit (0-9), zero otherwise
 */
int isdigit(int c) __attribute_const __no_throw;

/**
 * @brief Test if character is alphanumeric
 * @param c Character to test
 * @return Non-zero if c is a letter or digit, zero otherwise
 */
int isalnum(int c) __attribute_const __no_throw;

/**
 * @brief Test if character is uppercase letter
 * @param c Character to test
 * @return Non-zero if c is an uppercase letter (A-Z), zero otherwise
 */
int isupper(int c) __attribute_const __no_throw;

/**
 * @brief Test if character is lowercase letter
 * @param c Character to test
 * @return Non-zero if c is a lowercase letter (a-z), zero otherwise
 */
int islower(int c) __attribute_const __no_throw;

/**
 * @brief Test if character is hexadecimal digit
 * @param c Character to test
 * @return Non-zero if c is hex digit (0-9, A-F, a-f), zero otherwise
 */
int isxdigit(int c) __attribute_const __no_throw;

/**
 * @brief Test if character is a control character
 * @param c Character to test
 * @return Non-zero if c is a control character (0-31, 127), zero otherwise
 */
int iscntrl(int c) __attribute_const __no_throw;

/**
 * @brief Test if character is punctuation
 * @param c Character to test
 * @return Non-zero if c is punctuation (printable, non-alphanumeric), zero otherwise
 */
int ispunct(int c) __attribute_const __no_throw;

/**
 * @brief Test if character is whitespace
 * @param c Character to test
 * @return Non-zero if c is whitespace (space, tab, newline, etc.), zero otherwise
 */
int isspace(int c) __attribute_const __no_throw;

/**
 * @brief Test if character has graphical representation
 * @param c Character to test
 * @return Non-zero if c is printable and not a space, zero otherwise
 */
int isgraph(int c) __attribute_const __no_throw;

/**
 * @brief Test if character is printable
 * @param c Character to test
 * @return Non-zero if c is printable (including space), zero otherwise
 */
int isprint(int c) __attribute_const __no_throw;

/**
 * @brief Test if character is blank (space or tab)
 * @param c Character to test
 * @return Non-zero if c is space or horizontal tab, zero otherwise
 */
int isblank(int c) __attribute_const __no_throw;

#ifdef __cplusplus
}
#endif

#endif /* _CTYPE_H */
