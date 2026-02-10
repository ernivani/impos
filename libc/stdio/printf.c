#include <limits.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void uint_to_str(unsigned int value, char* buf, int base) {
	char tmp[32];
	int i = 0;
	if (value == 0) {
		buf[0] = '0';
		buf[1] = '\0';
		return;
	}
	while (value > 0) {
		unsigned int digit = value % base;
		tmp[i++] = digit < 10 ? '0' + digit : 'a' + digit - 10;
		value /= base;
	}
	int j = 0;
	while (i > 0)
		buf[j++] = tmp[--i];
	buf[j] = '\0';
}

static bool print(const char* data, size_t length) {
	const unsigned char* bytes = (const unsigned char*) data;
	for (size_t i = 0; i < length; i++)
		if (putchar(bytes[i]) == EOF)
			return false;
	return true;
}

/* Print 'count' copies of character 'c' */
static bool print_pad(char c, int count) {
	for (int i = 0; i < count; i++)
		if (putchar(c) == EOF)
			return false;
	return true;
}

/* Print a string with width/alignment padding */
static int print_padded(const char* str, int len, int width, int left_align, char pad_char) {
	int w = 0;
	int padding = width > len ? width - len : 0;
	if (!left_align && padding > 0) {
		if (!print_pad(pad_char, padding)) return -1;
		w += padding;
	}
	if (!print(str, len)) return -1;
	w += len;
	if (left_align && padding > 0) {
		if (!print_pad(' ', padding)) return -1;
		w += padding;
	}
	return w;
}

int printf(const char* restrict format, ...) {
	va_list parameters;
	va_start(parameters, format);

	int written = 0;

	while (*format != '\0') {
		if (format[0] != '%' || format[1] == '%') {
			if (format[0] == '%')
				format++;
			size_t amount = 1;
			while (format[amount] && format[amount] != '%')
				amount++;
			if (!print(format, amount))
				return -1;
			format += amount;
			written += amount;
			continue;
		}

		format++; /* skip '%' */

		/* Parse flags */
		int left_align = 0;
		int zero_pad = 0;
		while (*format == '-' || *format == '0') {
			if (*format == '-') left_align = 1;
			if (*format == '0') zero_pad = 1;
			format++;
		}
		if (left_align) zero_pad = 0; /* '-' overrides '0' */

		/* Parse width */
		int width = 0;
		while (*format >= '0' && *format <= '9') {
			width = width * 10 + (*format - '0');
			format++;
		}

		char pad_char = zero_pad ? '0' : ' ';

		if (*format == 'c') {
			format++;
			char c = (char) va_arg(parameters, int);
			int w = print_padded(&c, 1, width, left_align, ' ');
			if (w < 0) return -1;
			written += w;
		} else if (*format == 's') {
			format++;
			const char* str = va_arg(parameters, const char*);
			size_t len = strlen(str);
			int w = print_padded(str, (int)len, width, left_align, ' ');
			if (w < 0) return -1;
			written += w;
		} else if (*format == 'd') {
			format++;
			int value = va_arg(parameters, int);
			char buf[32];
			char numbuf[34]; /* sign + digits */
			int neg = 0;
			if (value < 0) {
				neg = 1;
				uint_to_str((unsigned int)(-(value + 1)) + 1, buf, 10);
			} else {
				uint_to_str((unsigned int)value, buf, 10);
			}
			int pos = 0;
			if (neg) numbuf[pos++] = '-';
			int blen = (int)strlen(buf);
			for (int i = 0; i < blen; i++)
				numbuf[pos++] = buf[i];
			numbuf[pos] = '\0';
			int w = print_padded(numbuf, pos, width, left_align, pad_char);
			if (w < 0) return -1;
			written += w;
		} else if (*format == 'x') {
			format++;
			unsigned int value = va_arg(parameters, unsigned int);
			char buf[32];
			uint_to_str(value, buf, 16);
			int len = (int)strlen(buf);
			int w = print_padded(buf, len, width, left_align, pad_char);
			if (w < 0) return -1;
			written += w;
		} else if (*format == 'u') {
			format++;
			unsigned int value = va_arg(parameters, unsigned int);
			char buf[32];
			uint_to_str(value, buf, 10);
			int len = (int)strlen(buf);
			int w = print_padded(buf, len, width, left_align, pad_char);
			if (w < 0) return -1;
			written += w;
		} else {
			/* Unknown specifier: output the '%' and the char */
			if (putchar('%') == EOF) return -1;
			written++;
			if (*format) {
				if (putchar(*format) == EOF) return -1;
				written++;
				format++;
			}
		}
	}

	va_end(parameters);
	return written;
}
