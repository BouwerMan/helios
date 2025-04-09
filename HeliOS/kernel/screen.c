#include <kernel/screen.h>
#include <string.h>

/* import our font that's in the object file we've created above */
extern char _binary_font_psf_start;
extern char _binary_font_psf_end;

uint16_t* unicode = NULL;

static struct screen_info sc = {
    .cx = 0,
    .cy = 0,
    .fgc = 0xFFFFFF,
    .bgc = 0x000000,
};

/* import our font that's in the object file we've created above */
extern char _binary_font_start[];

static void screen_putchar_at(unsigned short int c, int cx, int cy, uint32_t fg, uint32_t bg);
static void scroll();

// TODO: pass through framebuffer and such
void screen_init(struct limine_framebuffer* fb, uint32_t fg_color, uint32_t bg_color)
{
    // TODO: properly init psf, though the one im using currently isnt unicode

    sc.cx = 0;
    sc.cy = 0;
    sc.fgc = fg_color;
    sc.bgc = bg_color;
    sc.fb = fb;
    sc.fb_buffer = (char*)fb->address;
    sc.scanline = fb->pitch;
    sc.font = (PSF_font*)&_binary_font_psf_start;
}

void set_color(uint32_t fg, uint32_t bg)
{
    sc.fgc = fg;
    sc.bgc = bg;
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
        sc.cy++;
        sc.cx = 0;
        break;
    case '\b':
        if (sc.cx == 0) break;
        screen_putchar_at(' ', --sc.cx, sc.cy, sc.fgc, sc.bgc);
    default:
        screen_putchar_at(c, sc.cx++, sc.cy, sc.fgc, sc.bgc);
    }
    if (sc.cx >= sc.scanline) {
        sc.cx = 0;
        sc.cy++;
    }
    // TODO: Scrolling
    if (sc.cy >= sc.fb->height / sc.font->height) {
        scroll();
        sc.cy = sc.cy - 2;
    }
}

/**
 * @brief Scrolls the framebuffer content upward by one row.
 *
 * This function shifts the visible content of the framebuffer upward by one
 * row, effectively removing the topmost row and making space for new content
 * at the bottom. The last row is cleared and filled with the background color.
 *
 * Assumptions:
 * - The framebuffer (`sc.fb`) and font (`sc.font`) are properly initialized.
 * - The framebuffer uses a linear memory layout.
 */
static void scroll()
{
    uint32_t s_width = sc.fb->width;
    uint32_t char_height = sc.font->height;

    uint32_t scroll_height = (sc.fb->height / sc.font->height) - 1;
    uint64_t copy_size = char_height * s_width * (sc.fb->bpp / 8);

    void* addr = sc.fb->address;

    // Move everything up
    memcpy(addr, (void*)((uint64_t)addr + copy_size), copy_size * scroll_height);

    // Clear the last row
    uint64_t offset = copy_size * scroll_height;
    void* last_row = (void*)((uint64_t)addr + offset);
    memset(last_row, sc.bgc, copy_size);
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
    int offs = (cy * font->height * sc.scanline) + (cx * (font->width + 1) * sizeof(PIXEL));
    /* finally display pixels according to the bitmap */
    int x, y, line, mask;
    for (y = 0; y < font->height; y++) {
        /* save the starting position of the line */
        line = offs;
        mask = 1 << (font->width - 1);
        /* display a row */
        for (x = 0; x < font->width; x++) {
            *((PIXEL*)(sc.fb_buffer + line)) = *((unsigned int*)glyph) & mask ? fg : bg;
            /* adjust to the next pixel */
            mask >>= 1;
            line += sizeof(PIXEL);
        }
        /* adjust to the next line */
        glyph += bytesperline;
        offs += sc.scanline;
    }
}
