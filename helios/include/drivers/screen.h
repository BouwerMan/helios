/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <kernel/spinlock.h>
#include <kernel/types.h>
#include <limine.h>

static constexpr int CHAR_SPACING = 0;

enum COLORS {
	COLOR_WHITE = 0x00FFFFFF,
	COLOR_BLACK = 0x00000000,
	COLOR_RED = 0x00FF0000,
	COLOR_GREEN = 0x0000FF00,
	COLOR_BLUE = 0x000000FF,
};

static constexpr u16 PSF1_FONT_MAGIC = 0x0436;

typedef struct {
	uint16_t magic;	       // Magic bytes for identification.
	uint8_t fontMode;      // PSF font mode.
	uint8_t characterSize; // PSF character size.
} PSF1_Header;

static constexpr u32 PSF_FONT_MAGIC = 0x864ab572;

typedef struct {
	uint32_t magic;		/* magic bytes to identify PSF */
	uint32_t version;	/* zero */
	uint32_t headersize;	/* offset of bitmaps in file, 32 */
	uint32_t flags;		/* 0 if there's no unicode table */
	uint32_t numglyph;	/* number of glyphs */
	uint32_t bytesperglyph; /* size of each glyph */
	uint32_t height;	/* height in pixels */
	uint32_t width;		/* width in pixels */
} PSF_font;

typedef u32 PIXEL;		/* pixel pointer */

struct screen_info {
	size_t cx;		// Cursor position x
	size_t cy;		// Cursor position y
	uint32_t fgc;		// foregound color
	uint32_t bgc;		// background color
	uint64_t scanline;	// Number of bytes in each line
	uint32_t char_width;
	uint32_t char_height;
	size_t bytesperline;
	struct limine_framebuffer* fb;
	char* fb_buffer;
	PSF_font* font; // Font info
	spinlock_t lock;
};

void screen_init(uint32_t fg_color, uint32_t bg_color);
void __screen_clear();
void set_color(uint32_t fg, uint32_t bg);
void screen_putstring(const char* s);

/**
 * @brief Scrolls the framebuffer content upward by one row.
 */
void scroll();

/**
 * @brief Draws a character at a specific position on the screen with specified colors.
 *
 * @param c  The Unicode character to display.
 * @param cx The x-coordinate of the cursor position (in characters, not pixels).
 * @param cy The y-coordinate of the cursor position (in characters, not pixels).
 * @param fg The foreground color (e.g., 0xFFFFFF for white).
 * @param bg The background color (e.g., 0x000000 for black).
 */
void screen_putchar_at(uint16_t c,
		       size_t cx,
		       size_t cy,
		       uint32_t fg,
		       uint32_t bg);

void screen_putchar(char c);

struct screen_info* get_screen_info();

void screen_draw_cursor_at(size_t cx, size_t cy);
