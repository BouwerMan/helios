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

static bool is_shifted = false;
static bool is_ctrl_pressed = false;
static bool is_alt_pressed = false;
static bool caps_lock_on = false;

char process_scancode()
{
	unsigned char scancode = inb(0x60);
	if (scancode & 0x80) {
		// Key is released

		scancode &= 0x7F; // Remove release bit

		if (scancode == 42 || scancode == 54) {
			is_shifted = false;
		}
		return 0;
	} else {
		// Key pressed

		if (scancode == 42 || scancode == 54) { // Left or right shift
			is_shifted = true;
			return 0; // Don't output shift key itself
		}
		if (is_shifted) {
			return (char)kbdus_shifted[scancode];
		} else {
			return (char)kbdus[scancode];
		}
	}
}

void keyboard_interrupt_handler(struct registers* r)
{
	(void)r;
	char c = process_scancode();
	if (c != 0) {
		struct tty* console_tty = find_tty_by_name("tty0");
		tty_add_input_char(console_tty, c);
	}
}

void keyboard_init()
{
	isr_install_handler(IRQ1, keyboard_interrupt_handler);
}
