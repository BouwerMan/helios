#pragma once
#include <kernel/spinlock.h>
#include <limine.h>
#include <stddef.h>
#include <stdint.h>

enum COLORS {
	COLOR_WHITE = 0x00FFFFFF,
	COLOR_BLACK = 0x00000000,
	COLOR_RED = 0x00FF0000,
	COLOR_GREEN = 0x0000FF00,
	COLOR_BLUE = 0x000000FF,
};

#define PSF1_FONT_MAGIC 0x0436

typedef struct {
	uint16_t magic;	       // Magic bytes for identification.
	uint8_t fontMode;      // PSF font mode.
	uint8_t characterSize; // PSF character size.
} PSF1_Header;

#define PSF_FONT_MAGIC 0x864ab572

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

#define PIXEL uint32_t /* pixel pointer */

struct screen_info {
	size_t cx;    // Cursor position x
	size_t cy;    // Cursor position y
	uint32_t fgc; // foregound color
	uint32_t bgc; // background color
	int scanline; // Number of bytes in each line
	struct limine_framebuffer* fb;
	char* fb_buffer;
	PSF_font* font; // Font info
	spinlock_t lock;
};

void screen_init(struct limine_framebuffer* fb, uint32_t fg_color, uint32_t bg_color);
void screen_clear();
void set_color(uint32_t fg, uint32_t bg);
void screen_putstring(const char* s);
void screen_putchar(char c);
