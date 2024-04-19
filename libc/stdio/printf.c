#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

// Testing
#include <kernel/tty.h>

#define BUF_SIZE 1024

static char buffer[BUF_SIZE] = { '\0' };
static size_t pointer = 0;

static void parse_hex(unsigned int value, bool cap)
{
    const char* fmt = cap ? "0123456789ABCDEF" : "0123456789abcdef";
    bool zero = true; // Stores if all digits so far have been zero
    for (int i = 7; i >= 0; i--) {
        char c = fmt[(value >> (i * 4)) & 0xF];
        // if we have only seen zeros, we just skip them
        if (zero && c == '0') continue;
        zero = false;
        buffer[pointer++] = c;
    }
}

static void parse_num(unsigned int value, unsigned int base)
{

    size_t i = 0;
    int temp_buffer[16] = { 0 };

    // Gets digit from right to left and converts to ascii
    do {
        temp_buffer[i] = (value % base) + '0';
        i++;
        value /= base;
    } while (value != 0);
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
    if (strlen(format) > BUF_SIZE) return EOVERFLOW;

    pointer = 0;
    memset(buffer, 0, BUF_SIZE);

    while (*format != '\0') {

        if (pointer >= BUF_SIZE) return EOVERFLOW;

        // Check if no special option is present
        if ((format[0] != '%') && (format[0] != '\\')) {
            buffer[pointer++] = format[0];
            format++;
            continue;
        } // else if (format[0] == '\\') {
        //     switch (format[0]) {
        //     case '\\':
        //         buffer[pointer++] = '\\';
        //         break;
        //     case 'n':
        //         buffer[pointer++] = '\n';
        //         break;
        //         format++;
        //         continue;
        //     }
        // }

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
            unsigned int value = va_arg(args, unsigned int);
            parse_hex(value, false);
            break;
        }
        case 'X': {
            unsigned int value = va_arg(args, unsigned int);
            parse_hex(value, true);
            break;
        }
        case 'd':
        case 'i': {
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

int sprintf(char* str, const char* __restrict format, ...)
{
    va_list args;
    va_start(args, format);
    int result = vprintf(format, args);
    str = buffer;
    va_end(args);
    return result;
}

#if defined(__is_libk)
// TODO: Make this call puts and output full string instead of each char
static void print(const char* data) { tty_writestring(data); }

#else
// TODO: Proper libc print

#endif
