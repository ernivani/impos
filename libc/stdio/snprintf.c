#include <limits.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

int vsnprintf(char* str, size_t size, const char* format, va_list parameters) {
    if (!str || size == 0) {
        return 0;
    }

    size_t written = 0;

    /* Helper: write a single char to buffer */
    #define EMIT(c) do { if (written < size - 1) str[written] = (c); written++; } while(0)

    while (*format != '\0') {
        if (format[0] != '%' || format[1] == '%') {
            if (format[0] == '%')
                format++;
            EMIT(*format);
            format++;
            continue;
        }

        format++;  /* Skip % */

        /* Parse flags */
        int left_align = 0;
        int zero_pad = 0;
        while (*format == '-' || *format == '0') {
            if (*format == '-') left_align = 1;
            if (*format == '0') zero_pad = 1;
            format++;
        }
        if (left_align) zero_pad = 0;

        /* Parse width */
        int width = 0;
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (*format - '0');
            format++;
        }

        char pad_char = zero_pad ? '0' : ' ';

        if (*format == 'c') {
            format++;
            char c = (char)va_arg(parameters, int);
            int padding = width > 1 ? width - 1 : 0;
            if (!left_align)
                for (int i = 0; i < padding; i++) EMIT(' ');
            EMIT(c);
            if (left_align)
                for (int i = 0; i < padding; i++) EMIT(' ');
        } else if (*format == 's') {
            format++;
            const char* s = va_arg(parameters, const char*);
            int slen = (int)strlen(s);
            int padding = width > slen ? width - slen : 0;
            if (!left_align)
                for (int i = 0; i < padding; i++) EMIT(' ');
            for (int i = 0; i < slen; i++) EMIT(s[i]);
            if (left_align)
                for (int i = 0; i < padding; i++) EMIT(' ');
        } else if (*format == 'd') {
            format++;
            int value = va_arg(parameters, int);
            char buf[32];
            int pos = 0;

            if (value < 0) {
                unsigned int uval = (unsigned int)(-(value + 1)) + 1;
                char tmp[32];
                int ti = 0;
                if (uval == 0) { tmp[ti++] = '0'; }
                else { while (uval > 0) { tmp[ti++] = '0' + (uval % 10); uval /= 10; } }
                buf[pos++] = '-';
                while (ti > 0) buf[pos++] = tmp[--ti];
            } else {
                if (value == 0) { buf[pos++] = '0'; }
                else {
                    char tmp[32];
                    int ti = 0;
                    unsigned int uval = (unsigned int)value;
                    while (uval > 0) { tmp[ti++] = '0' + (uval % 10); uval /= 10; }
                    while (ti > 0) buf[pos++] = tmp[--ti];
                }
            }

            int padding = width > pos ? width - pos : 0;
            if (!left_align)
                for (int i = 0; i < padding; i++) EMIT(pad_char);
            for (int i = 0; i < pos; i++) EMIT(buf[i]);
            if (left_align)
                for (int i = 0; i < padding; i++) EMIT(' ');
        } else if (*format == 'u' || *format == 'x' || *format == 'X') {
            int is_hex = (*format == 'x' || *format == 'X');
            int is_upper = (*format == 'X');
            format++;
            unsigned int value = va_arg(parameters, unsigned int);
            int base = is_hex ? 16 : 10;
            char buf[32];
            int pos = 0;

            if (value == 0) {
                buf[pos++] = '0';
            } else {
                char tmp[32];
                int ti = 0;
                unsigned int temp = value;
                while (temp > 0) {
                    int digit = temp % base;
                    tmp[ti++] = digit < 10 ? '0' + digit : (is_upper ? 'A' : 'a') + digit - 10;
                    temp /= base;
                }
                while (ti > 0) buf[pos++] = tmp[--ti];
            }

            int padding = width > pos ? width - pos : 0;
            if (!left_align)
                for (int i = 0; i < padding; i++) EMIT(pad_char);
            for (int i = 0; i < pos; i++) EMIT(buf[i]);
            if (left_align)
                for (int i = 0; i < padding; i++) EMIT(' ');
        } else {
            EMIT('%');
            if (*format) {
                EMIT(*format);
                format++;
            }
        }
    }

    #undef EMIT

    if (written < size)
        str[written] = '\0';
    else
        str[size - 1] = '\0';
    return (int)written;
}

int snprintf(char* str, size_t size, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    int result = vsnprintf(str, size, format, ap);
    va_end(ap);
    return result;
}
