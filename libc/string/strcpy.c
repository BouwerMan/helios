#include <stdbool.h>
#include <string.h>

char* strcpy(char* dest, const char* src)
{
    char* original_dest = dest; // Maintains start of dest
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0'; // manually copy null termination
    return original_dest;
}

char* strncpy(char* dest, const char* src, size_t num)
{
    size_t i = 0;

    // Copy characters until we hit num or find '\0'
    while (i < num && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }

    // Fill the rest with '\0'
    while (i < num) {
        dest[i] = '\0';
        i++;
    }

    return dest;
}
