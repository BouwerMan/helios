#include <string.h>

char* strchr(const char* str, char character)
{
    while (*str) {
        if (*str == character) return (char*)str;
        str++;
    }
    // Check for character == '\0' explicitly
    return (character == '\0') ? (char*)str : NULL;
}
