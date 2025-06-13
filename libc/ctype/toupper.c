#include <ctype.h>
int toupper(int c)
{
	if ((unsigned char)c >= 97) return (unsigned char)c - 32;
	return (unsigned char)c;
}
