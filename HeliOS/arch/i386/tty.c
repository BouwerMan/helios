#include <kernel/asm.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/tty.h>

#include "vga.h"

static const size_t VGA_WIDTH = 80;
static const size_t VGA_HEIGHT = 25;

size_t terminal_row;
size_t terminal_column;
uint8_t terminal_color;
uint16_t* const terminal_buffer = (uint16_t* const)0xC00B8000;

void tty_initialize(void)
{
    terminal_row = 0;
    terminal_column = 0;
    terminal_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            terminal_buffer[index] = vga_entry(' ', terminal_color);
        }
    }
}

void tty_setcolor(uint8_t color)
{
    terminal_color = color;
}

void tty_putentryat(char c, uint8_t color, size_t x, size_t y)
{
    const size_t index = y * VGA_WIDTH + x; // position = (y_position * characters_per_line) + x_position
    terminal_buffer[index] = vga_entry(c, color);
}

void tty_putchar(char c)
{
    switch (c) {
    case '\n':
        terminal_row++;
        terminal_column = 0;
        break;
    case '\b':
        if (terminal_column == 0) break;
        tty_putentryat(' ', terminal_color, --terminal_column, terminal_row);
        break;
    default:
        tty_putentryat(c, terminal_color, terminal_column, terminal_row);
        terminal_column++;
    }
    // Handle overflowing on right side
    if (terminal_column >= VGA_WIDTH) {
        terminal_column = 0;
        terminal_row++;
    }

    size_t i, j;
    if (terminal_row >= VGA_HEIGHT) {
        for (i = 0; i < VGA_WIDTH; i++) {
            for (j = 0; j < VGA_HEIGHT; j++) {
                terminal_buffer[j * VGA_WIDTH + i] = terminal_buffer[(j + 1) * VGA_WIDTH + i];
            }
        }
        // Also clear out the bottom row
        for (i = 0; i < VGA_WIDTH; i++) {
            tty_putentryat(' ', terminal_color, i, VGA_HEIGHT - 1);
        }

        terminal_row = VGA_HEIGHT - 1;
    }
    tty_update_cursor(terminal_column, terminal_row + 1);
}

void tty_write(const char* data, size_t size)
{
    for (size_t i = 0; i < size; i++)
        tty_putchar(data[i]);
}

void tty_writestring(const char* data)
{
    tty_write(data, strlen(data));
}

void tty_enable_cursor(uint8_t cursor_start, uint8_t cursor_end)
{
    outb(0x3D4, 0x0A);
    outb(0x3D5, (inb(0x3D5) & 0xC0) | cursor_start);

    outb(0x3D4, 0x0B);
    outb(0x3D5, (inb(0x3D5) & 0xE0) | cursor_end);
}

void tty_disable_cursor()
{
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
}

void tty_update_cursor(uint16_t x, uint16_t y)
{
    uint16_t pos = y * VGA_WIDTH + x;

    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}
