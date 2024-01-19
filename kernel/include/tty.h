#pragma once

#include <stddef.h>
#include <stdint.h>

void tty_initialize(void);
void tty_putchar(char c);
void tty_write(const char* data, size_t size);
void tty_writestring(const char* data);

void tty_enable_cursor(uint8_t cursor_start, uint8_t cursor_end);
void tty_disable_cursor();
void tty_update_cursor(uint16_t x, uint16_t y);
