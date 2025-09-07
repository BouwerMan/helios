#include "ctype.h"

// Character classification bitmasks
#define _ALPHA	0x01 // alphabetic (a-z, A-Z)
#define _DIGIT	0x02 // digit (0-9)
#define _SPACE	0x04 // whitespace (' ', \t, \n, \r, \f, \v)
#define _PUNCT	0x08 // punctuation
#define _CNTRL	0x10 // control character (0-31, 127)
#define _UPPER	0x20 // uppercase letter (A-Z)
#define _LOWER	0x40 // lowercase letter (a-z)
#define _XDIGIT 0x80 // hexadecimal digit (0-9, A-F, a-f)

// Additional derived classifications:
// _GRAPH = _ALPHA | _DIGIT | _PUNCT (visible characters)
// _PRINT = _GRAPH | _SPACE (printable characters, but only regular space, not other whitespace)
// _BLANK = space and tab only
// _ALNUM = _ALPHA | _DIGIT

// Helper macro to build table entries
#define CTYPE(flags) (flags)

static constexpr unsigned char _ctype_table[256] = {
	// Control characters (0-31)
	['\0'] = CTYPE(_CNTRL),
	['\x01'] = CTYPE(_CNTRL),
	['\x02'] = CTYPE(_CNTRL),
	['\x03'] = CTYPE(_CNTRL),
	['\x04'] = CTYPE(_CNTRL),
	['\x05'] = CTYPE(_CNTRL),
	['\x06'] = CTYPE(_CNTRL),
	['\a'] = CTYPE(_CNTRL),
	['\b'] = CTYPE(_CNTRL),
	['\t'] = CTYPE(_CNTRL | _SPACE), // Tab is control + whitespace + blank
	['\n'] = CTYPE(_CNTRL | _SPACE),
	['\v'] = CTYPE(_CNTRL | _SPACE),
	['\f'] = CTYPE(_CNTRL | _SPACE),
	['\r'] = CTYPE(_CNTRL | _SPACE),
	['\x0E'] = CTYPE(_CNTRL),
	['\x0F'] = CTYPE(_CNTRL),
	['\x10'] = CTYPE(_CNTRL),
	['\x11'] = CTYPE(_CNTRL),
	['\x12'] = CTYPE(_CNTRL),
	['\x13'] = CTYPE(_CNTRL),
	['\x14'] = CTYPE(_CNTRL),
	['\x15'] = CTYPE(_CNTRL),
	['\x16'] = CTYPE(_CNTRL),
	['\x17'] = CTYPE(_CNTRL),
	['\x18'] = CTYPE(_CNTRL),
	['\x19'] = CTYPE(_CNTRL),
	['\x1A'] = CTYPE(_CNTRL),
	['\x1B'] = CTYPE(_CNTRL),
	['\x1C'] = CTYPE(_CNTRL),
	['\x1D'] = CTYPE(_CNTRL),
	['\x1E'] = CTYPE(_CNTRL),
	['\x1F'] = CTYPE(_CNTRL),

	// Space (printable whitespace)
	[' '] = CTYPE(_SPACE),

	// Punctuation (all printable, non-alphanumeric)
	['!'] = CTYPE(_PUNCT),
	['"'] = CTYPE(_PUNCT),
	['#'] = CTYPE(_PUNCT),
	['$'] = CTYPE(_PUNCT),
	['%'] = CTYPE(_PUNCT),
	['&'] = CTYPE(_PUNCT),
	['\''] = CTYPE(_PUNCT),
	['('] = CTYPE(_PUNCT),
	[')'] = CTYPE(_PUNCT),
	['*'] = CTYPE(_PUNCT),
	['+'] = CTYPE(_PUNCT),
	[','] = CTYPE(_PUNCT),
	['-'] = CTYPE(_PUNCT),
	['.'] = CTYPE(_PUNCT),
	['/'] = CTYPE(_PUNCT),

	// Digits (also hex digits)
	['0'] = CTYPE(_DIGIT | _XDIGIT),
	['1'] = CTYPE(_DIGIT | _XDIGIT),
	['2'] = CTYPE(_DIGIT | _XDIGIT),
	['3'] = CTYPE(_DIGIT | _XDIGIT),
	['4'] = CTYPE(_DIGIT | _XDIGIT),
	['5'] = CTYPE(_DIGIT | _XDIGIT),
	['6'] = CTYPE(_DIGIT | _XDIGIT),
	['7'] = CTYPE(_DIGIT | _XDIGIT),
	['8'] = CTYPE(_DIGIT | _XDIGIT),
	['9'] = CTYPE(_DIGIT | _XDIGIT),

	// More punctuation
	[':'] = CTYPE(_PUNCT),
	[';'] = CTYPE(_PUNCT),
	['<'] = CTYPE(_PUNCT),
	['='] = CTYPE(_PUNCT),
	['>'] = CTYPE(_PUNCT),
	['?'] = CTYPE(_PUNCT),
	['@'] = CTYPE(_PUNCT),

	// Uppercase letters (A-F are also hex digits)
	['A'] = CTYPE(_ALPHA | _UPPER | _XDIGIT),
	['B'] = CTYPE(_ALPHA | _UPPER | _XDIGIT),
	['C'] = CTYPE(_ALPHA | _UPPER | _XDIGIT),
	['D'] = CTYPE(_ALPHA | _UPPER | _XDIGIT),
	['E'] = CTYPE(_ALPHA | _UPPER | _XDIGIT),
	['F'] = CTYPE(_ALPHA | _UPPER | _XDIGIT),
	['G'] = CTYPE(_ALPHA | _UPPER),
	['H'] = CTYPE(_ALPHA | _UPPER),
	['I'] = CTYPE(_ALPHA | _UPPER),
	['J'] = CTYPE(_ALPHA | _UPPER),
	['K'] = CTYPE(_ALPHA | _UPPER),
	['L'] = CTYPE(_ALPHA | _UPPER),
	['M'] = CTYPE(_ALPHA | _UPPER),
	['N'] = CTYPE(_ALPHA | _UPPER),
	['O'] = CTYPE(_ALPHA | _UPPER),
	['P'] = CTYPE(_ALPHA | _UPPER),
	['Q'] = CTYPE(_ALPHA | _UPPER),
	['R'] = CTYPE(_ALPHA | _UPPER),
	['S'] = CTYPE(_ALPHA | _UPPER),
	['T'] = CTYPE(_ALPHA | _UPPER),
	['U'] = CTYPE(_ALPHA | _UPPER),
	['V'] = CTYPE(_ALPHA | _UPPER),
	['W'] = CTYPE(_ALPHA | _UPPER),
	['X'] = CTYPE(_ALPHA | _UPPER),
	['Y'] = CTYPE(_ALPHA | _UPPER),
	['Z'] = CTYPE(_ALPHA | _UPPER),

	// More punctuation
	['['] = CTYPE(_PUNCT),
	['\\'] = CTYPE(_PUNCT),
	[']'] = CTYPE(_PUNCT),
	['^'] = CTYPE(_PUNCT),
	['_'] = CTYPE(_PUNCT),
	['`'] = CTYPE(_PUNCT),

	// Lowercase letters (a-f are also hex digits)
	['a'] = CTYPE(_ALPHA | _LOWER | _XDIGIT),
	['b'] = CTYPE(_ALPHA | _LOWER | _XDIGIT),
	['c'] = CTYPE(_ALPHA | _LOWER | _XDIGIT),
	['d'] = CTYPE(_ALPHA | _LOWER | _XDIGIT),
	['e'] = CTYPE(_ALPHA | _LOWER | _XDIGIT),
	['f'] = CTYPE(_ALPHA | _LOWER | _XDIGIT),
	['g'] = CTYPE(_ALPHA | _LOWER),
	['h'] = CTYPE(_ALPHA | _LOWER),
	['i'] = CTYPE(_ALPHA | _LOWER),
	['j'] = CTYPE(_ALPHA | _LOWER),
	['k'] = CTYPE(_ALPHA | _LOWER),
	['l'] = CTYPE(_ALPHA | _LOWER),
	['m'] = CTYPE(_ALPHA | _LOWER),
	['n'] = CTYPE(_ALPHA | _LOWER),
	['o'] = CTYPE(_ALPHA | _LOWER),
	['p'] = CTYPE(_ALPHA | _LOWER),
	['q'] = CTYPE(_ALPHA | _LOWER),
	['r'] = CTYPE(_ALPHA | _LOWER),
	['s'] = CTYPE(_ALPHA | _LOWER),
	['t'] = CTYPE(_ALPHA | _LOWER),
	['u'] = CTYPE(_ALPHA | _LOWER),
	['v'] = CTYPE(_ALPHA | _LOWER),
	['w'] = CTYPE(_ALPHA | _LOWER),
	['x'] = CTYPE(_ALPHA | _LOWER),
	['y'] = CTYPE(_ALPHA | _LOWER),
	['z'] = CTYPE(_ALPHA | _LOWER),

	// Final punctuation and DEL
	['{'] = CTYPE(_PUNCT),
	['|'] = CTYPE(_PUNCT),
	['}'] = CTYPE(_PUNCT),
	['~'] = CTYPE(_PUNCT),
	['\x7F'] = CTYPE(_CNTRL), // DEL

				  // Extended ASCII entries default to 0
};

// Basic classification functions
int isalpha(int c)
{
	return (unsigned)c < 256 && (_ctype_table[c] & _ALPHA);
}

int isdigit(int c)
{
	return (unsigned)c < 256 && (_ctype_table[c] & _DIGIT);
}

int isalnum(int c)
{
	return (unsigned)c < 256 && (_ctype_table[c] & (_ALPHA | _DIGIT));
}

int isupper(int c)
{
	return (unsigned)c < 256 && (_ctype_table[c] & _UPPER);
}

int islower(int c)
{
	return (unsigned)c < 256 && (_ctype_table[c] & _LOWER);
}

int isxdigit(int c)
{
	return (unsigned)c < 256 && (_ctype_table[c] & _XDIGIT);
}

int iscntrl(int c)
{
	return (unsigned)c < 256 && (_ctype_table[c] & _CNTRL);
}

int ispunct(int c)
{
	return (unsigned)c < 256 && (_ctype_table[c] & _PUNCT);
}

int isspace(int c)
{
	return (unsigned)c < 256 && (_ctype_table[c] & _SPACE);
}

// Derived classification functions
int isgraph(int c)
{
	return (unsigned)c < 256 &&
	       (_ctype_table[c] & (_ALPHA | _DIGIT | _PUNCT));
}

int isprint(int c)
{
	return (unsigned)c < 256 &&
	       ((_ctype_table[c] & (_ALPHA | _DIGIT | _PUNCT)) || c == ' ');
}

int isblank(int c)
{
	return c == ' ' || c == '\t';
}

// Case conversion functions
int toupper(int c)
{
	return islower(c) ? c - 'a' + 'A' : c;
}

int tolower(int c)
{
	return isupper(c) ? c - 'A' + 'a' : c;
}
