#pragma once

#include <ctype.h>

int __toupper(int c) __attribute_const;
int __tolower(int c) __attribute_const;

int __isalpha(int c) __attribute_const;
int __isdigit(int c) __attribute_const;
int __isalnum(int c) __attribute_const;
int __isupper(int c) __attribute_const;
int __islower(int c) __attribute_const;
int __isxdigit(int c) __attribute_const;
int __iscntrl(int c) __attribute_const;
int __ispunct(int c) __attribute_const;
int __isspace(int c) __attribute_const;
int __isgraph(int c) __attribute_const;
int __isprint(int c) __attribute_const;
int __isblank(int c) __attribute_const;
