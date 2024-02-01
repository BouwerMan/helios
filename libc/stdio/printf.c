#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

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

static bool print(const char* data, size_t length)
{
        const unsigned char* bytes = (const unsigned char*)data;
        for (size_t i = 0; i < length; i++)
                if (putchar(bytes[i]) == EOF)
                        return false;
        return true;
}

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
