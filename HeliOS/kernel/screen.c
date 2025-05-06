#include <drivers/serial.h>
#include <kernel/screen.h>
#include <kernel/spinlock.h>
#include <kernel/tasks/scheduler.h>
#include <kernel/timer.h>
#include <string.h>
#include <util/log.h>

/* import our font that's in the object file we've created above */
extern char _binary_fonts_font_psf_start;
extern char _binary_fonts_font_psf_end;

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
	sc.font = (PSF_font*)&_binary_fonts_font_psf_start;
	spinlock_init(&sc.lock);
	log_debug("Framebuffer at %p, scanline: %x", fb->address, sc.scanline);
	log_debug("Framebuffer stats: height = %lx, width = %lx, size in bytes = %lx", fb->height, fb->width,
		  fb->height * fb->width);
	log_debug("Font height: %x, font width: %x", sc.font->height, sc.font->width);
	log_debug("wrap option 1: %lx, option 2: %lx", fb->width / sc.font->width, fb->pitch / sc.font->width);
}

void screen_clear()
{
	spinlock_acquire(&sc.lock);
	memset(sc.fb_buffer, 0, sc.fb->pitch * sc.fb->height);
	sc.cx = 0;
	sc.cy = 0;
	spinlock_release(&sc.lock);
}

/**
 * @brief Sets the foreground and background colors for text rendering.
 * @param fg The foreground color (e.g., 0xFFFFFF for white).
 * @param bg The background color (e.g., 0x000000 for black).
 */
void set_color(uint32_t fg, uint32_t bg)
{
	sc.fgc = fg;
	sc.bgc = bg;
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
 *
 * @TODO: Need to implement a couple more special chars like \t
 */
void screen_putchar(char c)
{
	spinlock_acquire(&sc.lock);
	disable_preemption();
	switch (c) {
	case '\n':
		sc.cy++;
		sc.cx = 0;
		break;
	case '\b':
		if (sc.cx == 0) break;
		screen_putchar_at(' ', --sc.cx, sc.cy, sc.fgc, sc.bgc);
		break;
	default:
		// write_serial(c);
		// if (c == 'p') sleep(5000);
		screen_putchar_at(c, sc.cx++, sc.cy, sc.fgc, sc.bgc);
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
	enable_preemption();
	spinlock_release(&sc.lock);
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
static void screen_putchar_at(unsigned short int c, int cx, int cy, uint32_t fg, uint32_t bg)
{
	/* cast the address to PSF header struct */
	PSF_font* font = (PSF_font*)&_binary_fonts_font_psf_start;
	/* we need to know how many bytes encode one row */
	int bytesperline = (font->width + 7) / 8;
	/* unicode translation */
	if (unicode != NULL) {
		c = unicode[c];
	}
	/* get the glyph for the character. If there's no
       glyph for a given character, we'll display the first glyph. */
	unsigned char* glyph = (unsigned char*)&_binary_fonts_font_psf_start + font->headersize +
			       (c > 0 && c < font->numglyph ? c : 0) * font->bytesperglyph;
	/* calculate the upper left corner on screen where we want to display.
       we only do this once, and adjust the offset later. This is faster. */
	int offs = (cy * font->height * sc.scanline) + (cx * (font->width + 1) * sizeof(PIXEL));
	/* finally display pixels according to the bitmap */
	int x, y, line, mask;
	for (y = 0; (uint32_t)y < font->height; y++) {
		/* save the starting position of the line */
		line = offs;
		mask = 1 << (font->width - 1);
		/* display a row */
		for (x = 0; (uint32_t)x < font->width; x++) {
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
