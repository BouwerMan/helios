#include <kernel/screen.h>
#include <stddef.h>

/* import our font that's in the object file we've created above */
extern char _binary_font_psf_start;
extern char _binary_font_psf_end;

uint16_t* unicode = NULL;

/* the linear framebuffer */
static char* fb_buffer;
/* number of bytes in each line, it's possible it's not screen width * bytesperpixel! */
static int scanline;
/* import our font that's in the object file we've created above */
extern char _binary_font_start[];

size_t terminal_row = 0;
size_t terminal_column = 0;
uint32_t fgc = 0;
uint32_t bgc = 0;

// TODO: pass through framebuffer and such
void screen_init(struct limine_framebuffer* fb, uint32_t fg_color, uint32_t bg_color)
{
    // TODO: properly init psf, though the one im using currently isnt unicode
    fb_buffer = (char*)fb->address;
    scanline = fb->pitch;
    fgc = fg_color;
    bgc = bg_color;
}

void screen_putstring(const char* s)
{
    while (*s) {
        screen_putchar(*s);
        s++;
    }
}

void screen_putchar(char c)
{
    switch (c) {
    case '\n':
        terminal_row++;
        terminal_column = 0;
        break;
    case '\b':
        if (terminal_column == 0) break;
        screen_putchar_at(' ', --terminal_column, terminal_row, fgc, bgc);
    default:
        screen_putchar_at(c, terminal_column++, terminal_row, fgc, bgc);
    }
    if (terminal_column >= scanline) {
        terminal_column = 0;
        terminal_row++;
    }
    // TODO: Scrolling
}

static void screen_putchar_at(
    /* note that this is int, not char as it's a unicode character */
    unsigned short int c,
    /* cursor position on screen, in characters not in pixels */
    int cx, int cy,
    /* foreground and background colors, say 0xFFFFFF and 0x000000 */
    uint32_t fg, uint32_t bg)
{
    /* cast the address to PSF header struct */
    PSF_font* font = (PSF_font*)&_binary_font_psf_start;
    /* we need to know how many bytes encode one row */
    int bytesperline = (font->width + 7) / 8;
    /* unicode translation */
    if (unicode != NULL) {
        c = unicode[c];
    }
    /* get the glyph for the character. If there's no
       glyph for a given character, we'll display the first glyph. */
    unsigned char* glyph = (unsigned char*)&_binary_font_psf_start + font->headersize
        + (c > 0 && c < font->numglyph ? c : 0) * font->bytesperglyph;
    /* calculate the upper left corner on screen where we want to display.
       we only do this once, and adjust the offset later. This is faster. */
    int offs = (cy * font->height * scanline) + (cx * (font->width + 1) * sizeof(PIXEL));
    /* finally display pixels according to the bitmap */
    int x, y, line, mask;
    for (y = 0; y < font->height; y++) {
        /* save the starting position of the line */
        line = offs;
        mask = 1 << (font->width - 1);
        /* display a row */
        for (x = 0; x < font->width; x++) {
            *((PIXEL*)(fb_buffer + line)) = *((unsigned int*)glyph) & mask ? fg : bg;
            /* adjust to the next pixel */
            mask >>= 1;
            line += sizeof(PIXEL);
        }
        /* adjust to the next line */
        glyph += bytesperline;
        offs += scanline;
    }
}
