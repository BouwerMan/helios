/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _CTYPE_H
#define _CTYPE_H

#include <features.h>

#ifdef __cplusplus
extern "C" {
#endif

int toupper(int __c) __attribute_const __nothrow;
int tolower(int __c) __attribute_const __nothrow;

int isalpha(int __c) __attribute_const __nothrow;
int isdigit(int __c) __attribute_const __nothrow;
int isalnum(int __c) __attribute_const __nothrow;
int isupper(int __c) __attribute_const __nothrow;
int islower(int __c) __attribute_const __nothrow;
int isxdigit(int __c) __attribute_const __nothrow;
int iscntrl(int __c) __attribute_const __nothrow;
int ispunct(int __c) __attribute_const __nothrow;
int isspace(int __c) __attribute_const __nothrow;
int isgraph(int __c) __attribute_const __nothrow;
int isprint(int __c) __attribute_const __nothrow;
int isblank(int __c) __attribute_const __nothrow;

#ifdef __cplusplus
}
#endif

#endif /* _CTYPE_H */
