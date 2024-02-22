#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// Testing
#include <kernel/tty.h>

#define BUF_SIZE_NEW 1024

static char buffer_new[BUF_SIZE_NEW] = { '\0' };
static int pointer = -1;

static bool print(const char* data, size_t length)
{
    const unsigned char* bytes = (const unsigned char*)data;
    for (size_t i = 0; i < length; i++)
        if (putchar(bytes[i]) == EOF)
            return false;
    return true;
}

int nprintf(const char* restrict format, ...)
{
    va_list args;
    va_start(args, format);
    int result = nvprintf(format, args);
    print(buffer_new, strlen(buffer_new));
    va_end(args);
    return result;
}

int nvprintf(const char* restrict format, va_list args)
{
    int written = 0;
    pointer = 0;
    memset(buffer_new, 0, BUF_SIZE_NEW);

    while (*format != '\0') {
        size_t maxrem = INT_MAX - written;

        // Check if no special option is present
        if ((format[0] != '%') && (format[0] != '\\')) {
            buffer_new[pointer++] = format[0];
            format++;
            continue;
        } else if (format[0] == '\\') {
            switch (format[0]) {
            case 'n':
                buffer_new[pointer++] = '\n';
                break;
            case '\\':
                buffer_new[pointer++] = '\\';
                break;
            }
            format++;
            continue;
        }

        // Step forward past '%' to get argument and handle
        // format++;
        const char* s;
        unsigned int value;
        switch (*++format) {
        case 's':
            s = va_arg(args, const char*);
            while (*s)
                buffer_new[pointer++] = *s++;
            break;
        case 'c':
            buffer_new[pointer++] = (char)va_arg(args, int); // Char promotes to int
            break;
        case 'x':

            value = (unsigned int)va_arg(args, int);

            break;
        }
        format++;
        continue;
    }
    return 0;
}

#if 1
#define BUF_SIZE 16

char buffer[BUF_SIZE];

// TODO: MAKE THIS BETTER I AM LITERALLY BANDAIDING THE ENTIRE THING
static char* parse_num(unsigned int value, unsigned int base)
{
    size_t i = 0;
    memset(buffer, 0, BUF_SIZE);

    // Gets digit from right to left and converts to ascii
    while (value != 0) {
        buffer[i] = (value % 10) + '0';
        i++;
        value /= 10;
    };
    // Iterates over array and prints it
    for (i; i > 0; i--) {
        putchar(buffer[i - 1]); // TODO: WTF is this
    }
    return buffer;
}

static char* parse_hex(unsigned int value)
{
    int i = 8;
    size_t index = 0;
    memset(buffer, 0, BUF_SIZE);

    while (i-- > 0)
        buffer[index++] = "0123456789abcdef"[(value >> (i * 4)) & 0xF];
    return buffer;
}

#if 0
static bool print(const char* data, size_t length)
{
    const unsigned char* bytes = (const unsigned char*)data;
    for (size_t i = 0; i < length; i++)
        if (putchar(bytes[i]) == EOF)
            return false;
    return true;
}
#endif

int printf(const char* restrict format, ...)
{
    va_list parameters;
    va_start(parameters, format);

    int written = 0;

    while (*format != '\0') {
        size_t maxrem = INT_MAX - written;

        if (format[0] != '%' || format[1] == '%') {
            if (format[0] == '%')
                format++;
            size_t amount = 1;
            while (format[amount] && format[amount] != '%')
                amount++;
            if (maxrem < amount) {
                // TODO: Set errno to EOVERFLOW.
                return -1;
            }
            if (!print(format, amount))
                return -1;
            format += amount;
            written += amount;
            continue;
        }

        const char* format_begun_at = format++;

        if (*format == 'c') {
            format++;
            char c = (char)va_arg(parameters, int /* char promotes to int */);
            if (!maxrem) {
                // TODO: Set errno to EOVERFLOW.
                return -1;
            }
            if (!print(&c, sizeof(c)))
                return -1;
            written++;
        } else if (*format == 's') {
            format++;
            const char* str = va_arg(parameters, const char*);
            size_t len = strlen(str);
            if (maxrem < len) {
                // TODO: Set errno to EOVERFLOW.
                return -1;
            }
            if (!print(str, len))
                return -1;
            written += len;
        } else if (*format == 'x') { // I've stolen this which is why it is kinda a mess/different style.
            format++;
            unsigned int value = (unsigned int)va_arg(parameters, int);
            parse_hex(value);
            size_t len = strlen(buffer);
            print(buffer, len);
            written += len;
        } else if (*format == 'd') {
            format++;
            unsigned int value = (unsigned int)va_arg(parameters, int);
            parse_num(value, 10);
            size_t len = strlen(buffer);
            // print(buffer, len);
            written += len;
        } else {
            format = format_begun_at;
            size_t len = strlen(format);
            if (maxrem < len) {
                // TODO: Set errno to EOVERFLOW.
                return -1;
            }
            if (!print(format, len))
                return -1;
            written += len;
            format += len;
        }
    }

    va_end(parameters);
    return written;
}
#endif
