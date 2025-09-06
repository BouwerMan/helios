#include "drivers/term.h"
#include "drivers/screen.h"
#include "lib/string.h"

enum parser_state {
	PARSER_NORMAL, /**< Normal character processing state */
	PARSER_ESCAPE, /**< ESC character received, waiting for next char */
	PARSER_CSI,    /**< Control Sequence Introducer (ESC[) state */
	PARSER_OSC,    /**< Operating System Command (ESC]) state */
};

enum ASCII_CONTROL {
	BEL = 0x07, /**< Bell */
	BS = 0x08,  /**< Backspace */
	HT = 0x09,  /**< Horizontal Tab */
	LF = 0x0A,  /**< Line Feed */
	VT = 0x0B,  /**< Vertical Tab */
	FF = 0x0C,  /**< Form Feed */
	CR = 0x0D,  /**< Carriage Return */
	ESC = 0x1B, /**< Escape */
};

enum COLOR_CODES {
	BLACK_FG = 30,
	BLACK_BG = 40,

	RED_FG = 31,
	RED_BG = 41,

	GREEN_FG = 32,
	GREEN_BG = 42,

	YELLOW_FG = 33,
	YELLOW_BG = 43,

	BLUE_FG = 34,
	BLUE_BG = 44,

	MAGENTA_FG = 35,
	MAGENTA_BG = 45,

	CYAN_FG = 36,
	CYAN_BG = 46,

	WHITE_FG = 37,
	WHITE_BG = 47,

	DEFAULT_FG = 39,
	DEFAULT_BG = 49,
};

enum SGR_PARAMS {
	SGR_RESET = 0,
	SGR_BOLD = 1,
	// SGR_UNDERLINE = 4,
	// SGR_REVERSE = 7,
	// SGR_BLINK = 5,
};

static uint32_t ansi_colors[] = {
	0x000000, // Black
	0xFF0000, // Red
	0x00FF00, // Green
	0xFFFF00, // Yellow
	0x0000FF, // Blue
	0xFF00FF, // Magenta
	0x00FFFF, // Cyan
	0xFFFFFF, // White
};

struct terminal_attrs {
	uint32_t fg_color;
	uint32_t bg_color;
	uint8_t flags; // Bold, underline, reverse, etc.
};

#define ATTR_BOLD      (1 << 0)
#define ATTR_UNDERLINE (1 << 1)
#define ATTR_REVERSE   (1 << 2)
#define ATTR_BLINK     (1 << 3)

struct terminal {
	struct screen_info* sc;

	// Terminal dimensions (in characters)
	size_t rows;
	size_t cols;

	// Current cursor position (in character coordinates)
	size_t cursor_x;
	size_t cursor_y;

	// Escape sequence parser state
	enum parser_state state;
	char param_buffer[32]; // For collecting escape parameters
	size_t param_len;
	int params[8];	       // Parsed numeric parameters
	size_t param_count;

	// Terminal state
	struct terminal_attrs current_attrs;
	struct terminal_attrs default_attrs;

	// Saved cursor state (for save/restore operations)
	size_t saved_x, saved_y;
	struct terminal_attrs saved_attrs;

	// Scroll region (0 = full screen)
	size_t scroll_top;
	size_t scroll_bottom;

	// Terminal modes/flags
	uint32_t mode_flags;

	spinlock_t lock; // Separate from screen_info lock
};

static struct terminal_attrs default_attrs = {
	.fg_color = 0xFFFFFF, // White
	.bg_color = 0x000000, // Black
	.flags = 0,
};

static struct terminal g_terminal;

static void handle_escape_char(char c);
static void process_sgr_param(int param);
static void handle_csi_char(char c);
static void handle_sgr_seq();

void term_init()
{
	struct screen_info* sc = get_screen_info();
	g_terminal.sc = sc;
	g_terminal.cols = sc->fb->width / (sc->font->width + 1);
	g_terminal.rows = sc->fb->height / sc->font->height;

	g_terminal.cursor_x = 0;
	g_terminal.cursor_y = 0;

	g_terminal.state = PARSER_NORMAL;
	g_terminal.param_len = 0;
	g_terminal.param_count = 0;

	g_terminal.current_attrs = default_attrs;
	g_terminal.default_attrs = default_attrs;

	g_terminal.saved_x = 0;
	g_terminal.saved_y = 0;

	g_terminal.scroll_top = 0;
	g_terminal.scroll_bottom = g_terminal.rows - 1;

	g_terminal.mode_flags = 0;

	spin_init(&g_terminal.lock);
}

void term_clear()
{
	__screen_clear();
	g_terminal.cursor_x = 0;
	g_terminal.cursor_y = 0;
}

void term_write(const char* s, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		term_putchar(s[i]);
	}
}

void term_putchar(char c)
{
	unsigned long flags;
	spin_lock_irqsave(&g_terminal.lock, &flags);

	switch (g_terminal.state) {
	case PARSER_NORMAL: {
		if (c == ESC) {
			// GDB BREAKPOINT
			g_terminal.state = PARSER_ESCAPE;

			// reset parser state
			g_terminal.param_len = 0;
			g_terminal.param_count = 0;
		} else {
			__term_putchar(c);
		}
		break;
	}
	case PARSER_ESCAPE: {
		// Handle post-ESC characters
		handle_escape_char(c);
		break;
	}
	case PARSER_CSI: {
		handle_csi_char(c);
		break;
	}
	case PARSER_OSC: {
		g_terminal.state = PARSER_NORMAL;
		log_error("Unhandled OSC sequence");
		break;
	}
	}

	spin_unlock_irqrestore(&g_terminal.lock, flags);
}

// Expects to be called with g_terminal.lock held
void __term_putchar(char c)
{
	switch (c) {
	case '\n':
		g_terminal.cursor_y++;
		g_terminal.cursor_x = 0;
		break;
	case '\b':
		if (g_terminal.cursor_x == 0) break;
		screen_putchar_at(' ',
				  --g_terminal.cursor_x,
				  g_terminal.cursor_y,
				  g_terminal.current_attrs.fg_color,
				  g_terminal.current_attrs.bg_color);
		break;
	case '\t':
		g_terminal.cursor_x = (g_terminal.cursor_x + 4) & ~3ULL;
		break;
	default:
		screen_putchar_at((uint16_t)c,
				  g_terminal.cursor_x++,
				  g_terminal.cursor_y,
				  g_terminal.current_attrs.fg_color,
				  g_terminal.current_attrs.bg_color);
		break;
	}

	if (g_terminal.cursor_x >= g_terminal.cols) {
		g_terminal.cursor_x = 0;
		g_terminal.cursor_y++;
	}
	if (g_terminal.cursor_y >= g_terminal.rows) {
		scroll();
		g_terminal.cursor_y = g_terminal.cursor_y - 1;
		g_terminal.cursor_x = 0;
	}
}

static void handle_escape_char(char c)
{
	switch (c) {
	case '[':
		g_terminal.state = PARSER_CSI;
		break;
	}
}

static void process_sgr_param(int param)
{
	switch (param) {
	case SGR_RESET:
		// Reset all attributes
		g_terminal.current_attrs = g_terminal.default_attrs;
		return;
	case SGR_BOLD:
		g_terminal.current_attrs.flags |= ATTR_BOLD;
		return;
		// Figure I may as well handle these here as well
	case DEFAULT_FG:
		g_terminal.current_attrs.fg_color =
			g_terminal.default_attrs.fg_color;
		return;
	case DEFAULT_BG:
		g_terminal.current_attrs.bg_color =
			g_terminal.default_attrs.bg_color;
		return;
	}

	// Handle foreground colors (30-37)
	if (param >= BLACK_FG && param <= WHITE_FG) {
		int color_index = param - BLACK_FG;
		g_terminal.current_attrs.fg_color = ansi_colors[color_index];
	}
	// Handle background colors (40-47)
	else if (param >= BLACK_BG && param <= WHITE_BG) {
		int color_index = param - BLACK_BG;
		g_terminal.current_attrs.bg_color = ansi_colors[color_index];
	}
}

static void handle_sgr_seq()
{
	// Handle empty parameter case (ESC[m)
	if (g_terminal.param_len == 0) {
		g_terminal.current_attrs = g_terminal.default_attrs;
		return;
	}

	g_terminal.param_buffer[g_terminal.param_len] = '\0';

	char* buffer = g_terminal.param_buffer;
	char* param_start = buffer;

	while (param_start <= buffer + g_terminal.param_len) {
		char* param_end = strchr(param_start, ';');
		if (!param_end) {
			param_end = buffer + g_terminal.param_len;
		}

		// Convert parameter to integer
		int param = 0;
		for (char* p = param_start; p < param_end; p++) {
			if (*p >= '0' && *p <= '9') {
				param = param * 10 + (*p - '0');
			}
		}

		process_sgr_param(param);

		param_start = param_end + 1;
		if (param_end == buffer + g_terminal.param_len) {
			break;
		}
	}
}

static void handle_csi_char(char c)
{
	switch (c) {
	case 'm': {
		// SGR - Select Graphic Rendition
		handle_sgr_seq();
		g_terminal.state = PARSER_NORMAL;
		break;
	default: {
		if (g_terminal.param_len >=
		    sizeof(g_terminal.param_buffer) - 1) {
			// Buffer overflow, reset state
			g_terminal.state = PARSER_NORMAL;
			log_error("CSI parameter buffer overflow");
			return;
		}
		g_terminal.param_buffer[g_terminal.param_len++] = c;
		break;
	}
	}
	}
}
