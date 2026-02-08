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

int printf(const char* restrict format, ...) {
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
			if (maxrem < amount)
				return -1;
			if (!print(format, amount))
				return -1;
			format += amount;
			written += amount;
			continue;
		}

		const char* format_begun_at = format++;

		if (*format == 'c') {
			format++;
			char c = (char) va_arg(parameters, int /* char promotes to int */);
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
			if (maxrem < len)
				return -1;
			if (!print(str, len))
				return -1;
			written += len;
		} else if (*format == 'd') {
			format++;
			int value = va_arg(parameters, int);
			char buf[32];
			int neg = 0;
			if (value < 0) {
				neg = 1;
				uint_to_str((unsigned int)(-(value + 1)) + 1, buf, 10);
			} else {
				uint_to_str((unsigned int)value, buf, 10);
			}
			size_t len = strlen(buf) + neg;
			if (maxrem < len)
				return -1;
			if (neg && !print("-", 1))
				return -1;
			if (!print(buf, strlen(buf)))
				return -1;
			written += len;
		} else if (*format == 'x') {
			format++;
			unsigned int value = va_arg(parameters, unsigned int);
			char buf[32];
			uint_to_str(value, buf, 16);
			size_t len = strlen(buf);
			if (maxrem < len) {
				return -1;
			}
			if (!print(buf, len))
				return -1;
			written += len;
		} else if (*format == 'u') {
			format++;
			unsigned int value = va_arg(parameters, unsigned int);
			char buf[32];
			uint_to_str(value, buf, 10);
			size_t len = strlen(buf);
			if (maxrem < len) {
				return -1;
			}
			if (!print(buf, len))
				return -1;
			written += len;
		} else {
			format = format_begun_at;
			size_t len = strlen(format);
			if (maxrem < len)
				return -1;
			if (!print(format, len))
				return -1;
			written += len;
			format += len;
		}
	}

	va_end(parameters);
	return written;
}