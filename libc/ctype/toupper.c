#include <ctype.h>
char toupper(char c)
{
    if (c >= 97) return c - 32;
    return c;
}
