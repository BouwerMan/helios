#include <string.h>

char* strcat(char* dest, const char* src)
{
    size_t dest_len = strlen(dest); // Finding null terminator of dest
    return strcpy(dest + dest_len, src);
}
