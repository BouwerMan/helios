#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// Testing
#include <kernel/tty.h>

#define BUF_SIZE 1024

static char buffer[BUF_SIZE] = { '\0' };
static int pointer = -1;

static void parse_num(unsigned int value, unsigned int base)
{

    size_t i = 0;
    int temp_buffer[16] = { 0 };

    // Gets digit from right to left and converts to ascii
    while (value != 0) {
        temp_buffer[i] = (value % base) + '0';
        i++;
        value /= base;
    };
    // Iterates over array and prints it
    for (; i > 0; i--) {
        buffer[pointer++] = temp_buffer[i - 1];
    }
}

int printf(const char* restrict format, ...)
{
    va_list args;
    va_start(args, format);
    int result = vprintf(format, args);
    // puts(buffer);
    print(buffer);
    va_end(args);
    return result;
}

// TODO: Buffer overflow protections
//       Error codes (including other supporting funcs)
int vprintf(const char* restrict format, va_list args)
{
    int written = 0;
    pointer = 0;
    memset(buffer, 0, BUF_SIZE);

    while (*format != '\0') {
        size_t maxrem = INT_MAX - written;

        // Check if no special option is present
        if ((format[0] != '%') && (format[0] != '\\')) {
            buffer[pointer++] = format[0];
            format++;
            continue;
        } else if (format[0] == '\\') {
            switch (format[0]) {
            case 'n':
                buffer[pointer++] = '\n';
                break;
            case '\\':
                buffer[pointer++] = '\\';
                break;
            }
            format++;
            continue;
        }

        // Step forward past '%' to get argument and handle
        // format++;
        switch (*++format) {
        case 's': {
            const char* s = va_arg(args, const char*);
            while (*s)
                buffer[pointer++] = *s++;
            break;
        }
        case 'c':
            buffer[pointer++] = (char)va_arg(args, int); // Char promotes to int
            break;
        case 'x': { // TODO: remove uneeded padding (go right to left?)
            int value = va_arg(args, unsigned int);
            int i = 8;
            while (i-- > 0)
                buffer[pointer++] = "0123456789abcdef"[(value >> (i * 4)) & 0xF];
            break;
        }
        case 'X': {
            int value = va_arg(args, unsigned int);
            int i = 8;
            while (i-- > 0)
                buffer[pointer++] = "0123456789ABCDEF"[(value >> (i * 4)) & 0xF];
            break;
        }
        case 'd': {
            int value = va_arg(args, int);
            if (value < 0) {
                buffer[pointer++] = '-';
                value *= -1;
            }
            parse_num(value, 10);
            break;
        }
        case 'u': {
            unsigned int value = va_arg(args, unsigned int);
            parse_num(value, 10);
            break;
        }
        case 'o': {
            unsigned int value = va_arg(args, unsigned int);
            parse_num(value, 8);
            break;
        }
        }
        format++;
        continue;
    }
    return pointer;
}

#if defined(__is_libk)
// TODO: Make this call puts and output full string instead of each char
static void print(const char* data)
{
    tty_writestring(data);
}

#else
// TODO: Proper libc print

#endif
