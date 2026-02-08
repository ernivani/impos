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

    while (*format != '\0' && written < size - 1) {
        if (format[0] != '%' || format[1] == '%') {
            if (format[0] == '%')
                format++;
            str[written++] = *format++;
            continue;
        }

        format++;  /* Skip % */

        if (*format == 'c') {
            format++;
            char c = (char)va_arg(parameters, int);
            if (written < size - 1) {
                str[written++] = c;
            }
        } else if (*format == 's') {
            format++;
            const char* s = va_arg(parameters, const char*);
            while (*s && written < size - 1) {
                str[written++] = *s++;
            }
        } else if (*format == 'd') {
            format++;
            int value = va_arg(parameters, int);
            char buf[32];
            int pos = 0;
            int neg = 0;

            if (value < 0) {
                neg = 1;
                value = -value;
            }

            if (value == 0) {
                buf[pos++] = '0';
            } else {
                int temp = value;
                int digits = 0;
                while (temp > 0) {
                    temp /= 10;
                    digits++;
                }
                pos = digits;
                temp = value;
                for (int i = digits - 1; i >= 0; i--) {
                    buf[i] = '0' + (temp % 10);
                    temp /= 10;
                }
            }

            if (neg && written < size - 1) {
                str[written++] = '-';
            }
            for (int i = 0; i < pos && written < size - 1; i++) {
                str[written++] = buf[i];
            }
        } else if (*format == 'u' || *format == 'x') {
            int is_hex = (*format == 'x');
            format++;
            unsigned int value = va_arg(parameters, unsigned int);
            char buf[32];
            int pos = 0;
            int base = is_hex ? 16 : 10;

            if (value == 0) {
                buf[pos++] = '0';
            } else {
                unsigned int temp = value;
                int digits = 0;
                while (temp > 0) {
                    temp /= base;
                    digits++;
                }
                pos = digits;
                temp = value;
                for (int i = digits - 1; i >= 0; i--) {
                    int digit = temp % base;
                    buf[i] = digit < 10 ? '0' + digit : 'a' + digit - 10;
                    temp /= base;
                }
            }

            for (int i = 0; i < pos && written < size - 1; i++) {
                str[written++] = buf[i];
            }
        } else {
            /* Unknown format, just copy it */
            if (written < size - 1) {
                str[written++] = '%';
            }
            if (*format && written < size - 1) {
                str[written++] = *format++;
            }
        }
    }

    str[written] = '\0';
    return written;
}

/* Simplified snprintf - only supports %s, %d, %u, %x */
int snprintf(char* str, size_t size, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    int result = vsnprintf(str, size, format, ap);
    va_end(ap);
    return result;
}
