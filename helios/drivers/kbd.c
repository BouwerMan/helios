#include "arch/idt.h"
#include "arch/regs.h"
#include "drivers/tty.h"

/* KBDUS means US Keyboard Layout. This is a scancode table
 *  used to layout a standard US keyboard. I have left some
 *  comments in to give you an idea of what key is what, even
 *  though I set it's array index to 0. You can change that to
 *  whatever you want using a macro, if you wish! */
unsigned char kbdus[128] = {
	0,    27,  '1', '2', '3',  '4', '5', '6', '7',	'8', /* 9 */
	'9',  '0', '-', '=', '\b',			     /* Backspace */
	'\t',						     /* Tab */
	'q',  'w', 'e', 'r',				     /* 19 */
	't',  'y', 'u', 'i', 'o',  'p', '[', ']', '\n',	     /* Enter key */
	0, /* 29   - Control */
	'a',  's', 'd', 'f', 'g',  'h', 'j', 'k', 'l',	';', /* 39 */
	'\'', '`', 0,					     /* Left shift */
	'\\', 'z', 'x', 'c', 'v',  'b', 'n',		     /* 49 */
	'm',  ',', '.', '/', 0,				     /* Right shift */
	'*',  0,					     /* Alt */
	' ',						     /* Space bar */
	0,						     /* Caps lock */
	0,					     /* 59 - F1 key ... > */
	0,    0,   0,	0,   0,	   0,	0,   0,	  0, /* < ... F10 */
	0,					     /* 69 - Num lock*/
	0,					     /* Scroll Lock */
	0,					     /* Home key */
	0,					     /* Up Arrow */
	0,					     /* Page Up */
	'-',  0,				     /* Left Arrow */
	0,    0,				     /* Right Arrow */
	'+',  0,				     /* 79 - End key*/
	0,					     /* Down Arrow */
	0,					     /* Page Down */
	0,					     /* Insert Key */
	0,					     /* Delete Key */
	0,    0,   0,	0,			     /* F11 Key */
	0,					     /* F12 Key */
	0, /* All other keys are undefined */
};

unsigned char kbdus_shifted[128] = {
	0,    27,  '!', '@', '#',  '$', '%', '^', '&',	'*', /* 9 */
	'(',  ')', '_', '+', '\b',			     /* Backspace */
	'\t',						     /* Tab */
	'Q',  'W', 'E', 'R',				     /* 19 */
	'T',  'Y', 'U', 'I', 'O',  'P', '{', '}', '\n',	     /* Enter key */
	0, /* 29   - Control */
	'A',  'S', 'D', 'F', 'G',  'H', 'J', 'K', 'L',	':', /* 39 */
	'"',  '~', 0,					     /* Left shift */
	'|',  'Z', 'X', 'C', 'V',  'B', 'N',		     /* 49 */
	'M',  '<', '>', '?', 0,				     /* Right shift */
	'*',  0,					     /* Alt */
	' ',						     /* Space bar */
	0,						     /* Caps lock */
	// ... rest same as kbdus (function keys, etc.)
};

typedef struct {
	enum { KEY_NONE, KEY_CHAR, KEY_SEQUENCE } type;
	char character;	      // for single chars
	const char* sequence; // for escape sequences
} key_result_t;

enum SPECIAL_KEYS {
	SC_LEFT_SHIFT = 0x2A,
	SC_RIGHT_SHIFT = 0x36,
	SC_LEFT_CTRL = 0x1D,
	SC_LEFT_ALT = 0x38,
	SC_CAPS_LOCK = 0x3A,
	SC_ARROW_UP = 0x48,
	SC_ARROW_DOWN = 0x50,
	SC_ARROW_LEFT = 0x4B,
	SC_ARROW_RIGHT = 0x4D,
	SC_RELEASE_MASK = 0x80
};

static const char* ARROW_UP = "\033[A";
static const char* ARROW_DOWN = "\033[B";
static const char* ARROW_RIGHT = "\033[C";
static const char* ARROW_LEFT = "\033[D";

static bool is_shifted = false;
static bool is_ctrl_pressed = false;
static bool is_alt_pressed = false;
static bool caps_lock_on = false;

[[maybe_unused]]
static key_result_t res_none = { .type = KEY_NONE,
				 .character = 0,
				 .sequence = nullptr };

static inline key_result_t make_char_result(char c)
{
	return (key_result_t) { .type = KEY_CHAR,
				.character = c,
				.sequence = nullptr };
}

static inline key_result_t make_sequence_result(const char* seq)
{
	return (key_result_t) { .type = KEY_SEQUENCE,
				.character = 0,
				.sequence = seq };
}

static inline key_result_t make_no_result(void)
{
	return (key_result_t) { .type = KEY_NONE };
}

static void handle_key_release(unsigned int scancode)
{
	enum SPECIAL_KEYS sk = (enum SPECIAL_KEYS)scancode;
	switch (sk) {
	case SC_LEFT_CTRL:   is_ctrl_pressed = false; break;
	case SC_LEFT_ALT:    is_alt_pressed = false; break;
	case SC_RIGHT_SHIFT:
	case SC_LEFT_SHIFT:  is_shifted = false; break;
	default:	     break; // Not a modifier key
	}
}

static bool handle_modifier_keys(unsigned char scancode)
{
	enum SPECIAL_KEYS sk = (enum SPECIAL_KEYS)scancode;
	switch (sk) {
	case SC_LEFT_CTRL:   is_ctrl_pressed = true; return true;
	case SC_LEFT_ALT:    is_alt_pressed = true; return true;
	case SC_RIGHT_SHIFT:
	case SC_LEFT_SHIFT:  is_shifted = true; return true;
	case SC_CAPS_LOCK:   caps_lock_on = !caps_lock_on; return true;
	default:	     return false; // Not a modifier key
	}
	return false;
}

static key_result_t handle_special_keys(unsigned char scancode)
{
	switch (scancode) {
	case SC_ARROW_UP:    return make_sequence_result(ARROW_UP);
	case SC_ARROW_DOWN:  return make_sequence_result(ARROW_DOWN);
	case SC_ARROW_LEFT:  return make_sequence_result(ARROW_LEFT);
	case SC_ARROW_RIGHT: return make_sequence_result(ARROW_RIGHT);
	default:	     return make_no_result();
	}
}

key_result_t process_scancode()
{
	unsigned char scancode = inb(0x60);

	if (handle_modifier_keys(scancode)) {
		return make_no_result();
	}

	if (scancode & SC_RELEASE_MASK) {
		handle_key_release(scancode & ~((unsigned int)SC_RELEASE_MASK));
		return make_no_result();
	}

	// Check for special keys (arrows, function keys, etc.)
	key_result_t special = handle_special_keys(scancode);
	if (special.type != KEY_NONE) {
		return special;
	}

	// Handle regular character keys
	unsigned char c = is_shifted ? kbdus_shifted[scancode] :
				       kbdus[scancode];

	// Apply control modifier if needed
	if (is_ctrl_pressed && c >= 'a' && c <= 'z') {
		c = c - 'a' + 1; // Convert to control character
	} else if (is_ctrl_pressed && c >= 'A' && c <= 'Z') {
		c = c - 'A' + 1; // Convert to control character
	}

	return c ? make_char_result((char)c) : make_no_result();
}

void keyboard_interrupt_handler(struct registers* r)
{
	(void)r;
	key_result_t key = process_scancode();

	struct tty* console_tty = find_tty_by_name("tty0");

	switch (key.type) {
	case KEY_NONE: return; // No action needed
	case KEY_CHAR: {
		tty_add_input_char(console_tty, key.character);
		break;
	}
	case KEY_SEQUENCE: {
		for (const char* p = key.sequence; p && *p; p++) {
			tty_add_input_char(console_tty, *p);
		}
		break;
	}
	}
}

void keyboard_init()
{
	isr_install_handler(IRQ1, keyboard_interrupt_handler);
}
