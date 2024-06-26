#include <stdint.h>
#include <string.h>

int strcmp(const char* str1, const char* str2)
{
    uint32_t i = 0;
    while (1) {
        if (str1[i] < str2[i])
            return -1;
        else if (str1[i] > str2[i])
            return 1;
        else {
            if (str1[i] == '\0') return 0;

            ++i;
        }
    }
}

int strncmp(const char* str1, const char* str2, size_t count)
{
    uint32_t i = 0;
    while (i < count) {
        if (str1[i] < str2[i])
            return -1;
        else if (str1[i] > str2[i])
            return 1;
        else {
            if (str1[i] == '\0') return 0;

            ++i;
        }
    }
    return 0;
}
