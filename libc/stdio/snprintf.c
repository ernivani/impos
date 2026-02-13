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

        /* Parse precision */
        int has_precision = 0;
        int precision = 0;
        if (*format == '.') {
            has_precision = 1;
            format++;
            while (*format >= '0' && *format <= '9') {
                precision = precision * 10 + (*format - '0');
                format++;
            }
        }

        /* Parse length modifier */
        int is_long = 0;
        if (*format == 'l') { is_long = 1; format++; }
        if (*format == 'l') { format++; } /* skip 'll' */

        /* For integers, precision overrides zero_pad */
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
            if (!s) s = "(null)";
            int slen = (int)strlen(s);
            /* Precision limits max chars for strings */
            if (has_precision && precision < slen)
                slen = precision;
            int padding = width > slen ? width - slen : 0;
            if (!left_align)
                for (int i = 0; i < padding; i++) EMIT(' ');
            for (int i = 0; i < slen; i++) EMIT(s[i]);
            if (left_align)
                for (int i = 0; i < padding; i++) EMIT(' ');
        } else if (*format == 'd' || *format == 'i') {
            format++;
            int value = va_arg(parameters, int);
            char digits[32];
            int ndigits = 0;
            int neg = 0;

            if (value < 0) {
                neg = 1;
                unsigned int uval = (unsigned int)(-(value + 1)) + 1;
                if (uval == 0) { digits[ndigits++] = '0'; }
                else { while (uval > 0) { digits[ndigits++] = '0' + (uval % 10); uval /= 10; } }
            } else {
                if (value == 0) { digits[ndigits++] = '0'; }
                else {
                    unsigned int uval = (unsigned int)value;
                    while (uval > 0) { digits[ndigits++] = '0' + (uval % 10); uval /= 10; }
                }
            }

            /* Precision: minimum number of digits (zero-padded) */
            int min_digits = has_precision ? precision : 1;
            int zero_fill = min_digits > ndigits ? min_digits - ndigits : 0;
            int num_len = neg + zero_fill + ndigits; /* total chars for the number */

            /* Width padding (precision overrides '0' flag) */
            char wpad = (has_precision) ? ' ' : pad_char;
            int padding = width > num_len ? width - num_len : 0;

            if (!left_align && wpad == ' ')
                for (int i = 0; i < padding; i++) EMIT(' ');
            if (neg) EMIT('-');
            if (!left_align && wpad == '0')
                for (int i = 0; i < padding; i++) EMIT('0');
            for (int i = 0; i < zero_fill; i++) EMIT('0');
            while (ndigits > 0) EMIT(digits[--ndigits]);
            if (left_align)
                for (int i = 0; i < padding; i++) EMIT(' ');
        } else if (*format == 'u' || *format == 'x' || *format == 'X') {
            int is_hex = (*format == 'x' || *format == 'X');
            int is_upper = (*format == 'X');
            format++;
            unsigned int value = va_arg(parameters, unsigned int);
            int base = is_hex ? 16 : 10;
            char digits[32];
            int ndigits = 0;

            if (value == 0) {
                digits[ndigits++] = '0';
            } else {
                unsigned int temp = value;
                while (temp > 0) {
                    int digit = temp % base;
                    digits[ndigits++] = digit < 10 ? '0' + digit : (is_upper ? 'A' : 'a') + digit - 10;
                    temp /= base;
                }
            }

            /* Precision: minimum number of digits */
            int min_digits = has_precision ? precision : 1;
            int zero_fill = min_digits > ndigits ? min_digits - ndigits : 0;
            int num_len = zero_fill + ndigits;

            char wpad = (has_precision) ? ' ' : pad_char;
            int padding = width > num_len ? width - num_len : 0;

            if (!left_align && wpad == ' ')
                for (int i = 0; i < padding; i++) EMIT(' ');
            if (!left_align && wpad == '0')
                for (int i = 0; i < padding; i++) EMIT('0');
            for (int i = 0; i < zero_fill; i++) EMIT('0');
            while (ndigits > 0) EMIT(digits[--ndigits]);
            if (left_align)
                for (int i = 0; i < padding; i++) EMIT(' ');
        } else if (*format == 'p') {
            format++;
            unsigned int value = (unsigned int)(uintptr_t)va_arg(parameters, void*);
            char buf[32];
            int pos = 0;
            buf[pos++] = '0'; buf[pos++] = 'x';
            if (value == 0) {
                buf[pos++] = '0';
            } else {
                char tmp[16];
                int ti = 0;
                unsigned int temp = value;
                while (temp > 0) {
                    int digit = temp % 16;
                    tmp[ti++] = digit < 10 ? '0' + digit : 'a' + digit - 10;
                    temp /= 16;
                }
                while (ti > 0) buf[pos++] = tmp[--ti];
            }
            for (int j = 0; j < pos; j++) EMIT(buf[j]);
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
