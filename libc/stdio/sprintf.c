#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

int sprintf(char* str, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    int result = vsnprintf(str, SIZE_MAX, format, ap);
    va_end(ap);
    return result;
}
