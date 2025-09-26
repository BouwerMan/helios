/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stddef.h>

void term_get_size(size_t* rows, size_t* cols);

void term_init();

void term_clear();

void term_write(const char* s, size_t len);

void term_putchar(char c);

// Direct putchar without any parsing
void __term_putchar(char c);
