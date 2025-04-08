#pragma once
#include <limine.h>
#include <stdint.h>

#define PSF1_FONT_MAGIC 0x0436

typedef struct {
    uint16_t magic;        // Magic bytes for identification.
    uint8_t fontMode;      // PSF font mode.
    uint8_t characterSize; // PSF character size.
} PSF1_Header;

#define PSF_FONT_MAGIC 0x864ab572

typedef struct {
    uint32_t magic;         /* magic bytes to identify PSF */
    uint32_t version;       /* zero */
    uint32_t headersize;    /* offset of bitmaps in file, 32 */
    uint32_t flags;         /* 0 if there's no unicode table */
    uint32_t numglyph;      /* number of glyphs */
    uint32_t bytesperglyph; /* size of each glyph */
    uint32_t height;        /* height in pixels */
    uint32_t width;         /* width in pixels */
} PSF_font;

#define PIXEL uint32_t /* pixel pointer */

void screen_init(struct limine_framebuffer* fb, uint32_t fg_color, uint32_t bg_color);
void screen_putstring(const char* s);
void screen_putchar(char c);
static void screen_putchar_at(unsigned short int c, int cx, int cy, uint32_t fg, uint32_t bg);
