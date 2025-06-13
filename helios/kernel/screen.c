/**
 * @file kernel/screen.c
 *
 * Copyright (C) 2025  Dylan Parks
 *
 * This file is part of HeliOS
 *
 * HeliOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <string.h>

#include <kernel/helios.h>
#include <kernel/limine_requests.h>
#include <kernel/screen.h>
#include <kernel/spinlock.h>
#include <util/log.h>

/* import our font that's in the object file we've created above */
extern char _binary_fonts_font_psf_start[];
extern char _binary_fonts_font_psf_end[];

/* import our font that's in the object file we've created above */
extern char _binary_font_start[];

uint16_t* unicode = NULL;

static struct screen_info sc = {
	.cx = 0,
	.cy = 0,
	.fgc = 0xFFFFFF,
	.bgc = 0x000000,
};

static void scroll();
static void screen_putchar_at(uint16_t c, size_t cx, size_t cy, uint32_t fg, uint32_t bg);
static inline void draw_glyph_scanline(const uint8_t* glyph_row, PIXEL* dst, uint32_t fg, uint32_t bg);
static inline void draw_glyph(uint8_t* glyph, size_t offset, uint32_t fg, uint32_t bg);

void screen_init(uint32_t fg_color, uint32_t bg_color)
{
	// TODO: properly init psf, though the one im using currently isnt unicode

	// Ensure we got a framebuffer.
	if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1) {
		for (;;)
			halt();
	}

	struct limine_framebuffer* fb = framebuffer_request.response->framebuffers[0];

	sc.cx = 0;
	sc.cy = 0;
	sc.fgc = fg_color;
	sc.bgc = bg_color;
	sc.fb = fb;
	sc.fb_buffer = (char*)fb->address;
	sc.scanline = fb->pitch;
	sc.font = (PSF_font*)&_binary_fonts_font_psf_start;
	sc.char_width = sc.font->width + CHAR_SPACING; // +1 for spacing
	sc.char_height = sc.font->height;
	sc.bytesperline = (sc.font->width + 7) / 8;
	spinlock_init(&sc.lock);

	log_debug("Framebuffer at %p, scanline: %lx", fb->address, sc.scanline);
	log_debug("Framebuffer stats: height = %lx, width = %lx, size in bytes = %lx", fb->height, fb->width,
		  fb->height * fb->width);
	log_debug("Font height: %x, font width: %x", sc.font->height, sc.font->width);
}

void screen_clear()
{
	// spinlock_acquire(&sc.lock);

	uintptr_t addr = (uintptr_t)sc.fb->address;
	uint64_t char_height = sc.char_height;
	uint64_t pitch = sc.fb->pitch;
	uint64_t height = sc.fb->height;
	size_t total_chars_y = height / char_height;
	size_t total_rows = (total_chars_y - 1) * char_height;

	// Clear scanline row by scanline row
	for (size_t row = 0; row < total_rows; row++) {
		void* dst = (void*)(addr + (row * pitch));
		memset(dst, 0, pitch);
	}

	sc.cx = 0;
	sc.cy = 0;

	// spinlock_release(&sc.lock);
}

/**
 * @brief Sets the foreground and background colors for text rendering.
 * @param fg The foreground color (e.g., 0xFFFFFF for white).
 * @param bg The background color (e.g., 0x000000 for black).
 */
void set_color(uint32_t fg, uint32_t bg)
{
	spinlock_acquire(&sc.lock);
	sc.fgc = fg;
	sc.bgc = bg;
	spinlock_release(&sc.lock);
}

/**
 * @brief Writes a null-terminated string to the screen.
 * @param s The null-terminated string to write to the screen.
 */
void screen_putstring(const char* s)
{
	while (*s) {
		screen_putchar(*s);
		s++;
	}
}

/**
 * @brief Writes a character to the screen at the current cursor position.
 *
 * This function handles special characters such as newline ('\n') and backspace ('\b').
 * For regular characters, it renders them at the current cursor position and advances
 * the cursor. If the cursor reaches the end of a line or the bottom of the screen,
 * it wraps to the next line or scrolls the screen, respectively.
 *
 * @param c The character to write to the screen.
 */
void screen_putchar(char c)
{
	spinlock_acquire(&sc.lock);

	switch (c) {
	case '\n':
		sc.cy++;
		sc.cx = 0;
		break;
	case '\b':
		if (sc.cx == 0) break;
		screen_putchar_at(' ', --sc.cx, sc.cy, sc.fgc, sc.bgc);
		break;
	case '\t':
		sc.cx = (sc.cx + 4) & ~3ULL;
		break;
	default:
		screen_putchar_at((uint16_t)c, sc.cx++, sc.cy, sc.fgc, sc.bgc);
		break;
	}

	if (sc.cx >= sc.fb->width / (sc.font->width + 1)) {
		sc.cx = 0;
		sc.cy++;
	}
	if (sc.cy >= sc.fb->height / sc.font->height) {
		scroll();
		sc.cy = sc.cy - 1;
		sc.cx = 0;
	}

	spinlock_release(&sc.lock);
}

/**
 * @brief Scrolls the framebuffer content upward by one row.
 *
 * This function shifts the visible content of the framebuffer upward by one
 * row, effectively removing the topmost row and making space for new content
 * at the bottom. The last row is cleared and filled with the background color.
 */
static void scroll()
{
	uintptr_t addr = (uintptr_t)sc.fb->address;
	uint64_t char_height = sc.char_height;
	uint64_t pitch = sc.fb->pitch;
	uint64_t height = sc.fb->height;

	size_t total_chars_y = height / char_height;
	size_t total_rows = (total_chars_y - 1) * char_height;

	// Move everything up, I'm doing it one scanline at a time
	for (size_t row = 0; row < total_rows; row++) {
		void* dst = (void*)(addr + (row * pitch));
		void* src = (void*)(addr + ((row + char_height) * pitch));
		memmove(dst, src, pitch);
	}

	// Clear the last row. Once again, one scanline at a time
	size_t start_y = (height / char_height - 1) * char_height;
	void* row_base = (void*)(addr + start_y * pitch);
	for (size_t y = 0; y < char_height; y++) {
		uint32_t* dst = (uint32_t*)((uintptr_t)row_base + (y * pitch));
		// TODO: This memset doesn't work if bgc is not 0xFFFFFF
		memset(dst, (int)sc.bgc, pitch);
	}
}

/**
 * @brief Draws a character at a specific position on the screen with specified colors.
 *
 * This function renders a Unicode character at the given cursor position
 * on the screen using the provided foreground and background colors. It
 * utilizes a PSF font to determine the glyph representation of the character.
 *
 * @param c  The Unicode character to display.
 * @param cx The x-coordinate of the cursor position (in characters, not pixels).
 * @param cy The y-coordinate of the cursor position (in characters, not pixels).
 * @param fg The foreground color (e.g., 0xFFFFFF for white).
 * @param bg The background color (e.g., 0x000000 for black).
 */
static void screen_putchar_at(uint16_t c, size_t cx, size_t cy, uint32_t fg, uint32_t bg)
{
	/* unicode translation */
	if (unicode != NULL) {
		c = unicode[c];
	}

	uint32_t numglyphs = sc.font->numglyph;
	uint32_t glyph_index = (c > 0 && c < numglyphs) ? c : 0;

	unsigned char* glyph = (unsigned char*)sc.font + sc.font->headersize + glyph_index * sc.font->bytesperglyph;
	size_t pixel_x = cx * sc.char_width;
	size_t pixel_y = cy * sc.char_height;
	size_t fb_offset = (pixel_y * sc.scanline) + (pixel_x * sizeof(PIXEL));
	draw_glyph(glyph, fb_offset, fg, bg);
}

/**
 * @brief Draws a single scanline of a glyph onto a framebuffer row.
 *
 * @param glyph_row     Pointer to the packed glyph row data (usually one row of the bitmap).
 * @param dst           Pointer to the framebuffer scanline to write to (one PIXEL per pixel).
 * @param width         Actual number of pixels to draw (may be < bytesperline * 8).
 * @param bytesperline  Number of bytes per glyph row in memory (usually (width + 7) / 8).
 * @param fg            Foreground color.
 * @param bg            Background color.
 */
static inline void draw_glyph_scanline(const uint8_t* glyph_row, PIXEL* dst, uint32_t fg, uint32_t bg)
{
	uint32_t pixel_index = 0; // Horizontal pixel index across the row

	for (size_t byte_i = 0; byte_i < sc.bytesperline; byte_i++) {
		uint8_t bits = glyph_row[byte_i];

		// Process bits from most significant (bit 7) to least significant (bit 0)
		for (int bit_i = 7; bit_i >= 0; bit_i--) {
			// Stop once we've drawn all requested pixels (for widths not divisible by 8)
			if (pixel_index >= sc.char_width) return;

			// Extract bit and assign appropriate color
			bool bit_set = (bits >> bit_i) & 1;
			dst[pixel_index++] = bit_set ? fg : bg;
		}
	}
}

/**
 * @brief Renders a full glyph bitmap to the framebuffer at a specified byte offset.
 *
 * This function iterates over each scanline of the provided glyph bitmap and renders
 * it into the framebuffer using the specified foreground and background colors.
 * It assumes the framebuffer is linear and that the glyph is encoded as a series
 * of packed bitmaps (one per row).
 *
 * @param glyph         Pointer to the glyph bitmap data (packed 1bpp format).
 * @param offset        Byte offset into the framebuffer where the top-left pixel of the glyph should be drawn.
 * @param width         Width of the glyph in pixels.
 * @param height        Height of the glyph in pixels.
 * @param fg            Foreground color (used for set bits).
 * @param bg            Background color (used for cleared bits).
 */
static inline void draw_glyph(uint8_t* glyph, size_t offset, uint32_t fg, uint32_t bg)
{
	for (size_t y = 0; y < sc.char_height; y++) {
		const uint8_t* glyph_row = glyph + (y * sc.bytesperline);
		PIXEL* dst_line = (PIXEL*)(sc.fb_buffer + offset + (y * sc.scanline));
		draw_glyph_scanline(glyph_row, dst_line, fg, bg);
	}
}
