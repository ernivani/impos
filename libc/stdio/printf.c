#include <stdarg.h>
#include <stdio.h>

int printf(const char* restrict format, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);

    /* Print what was actually written (may be truncated) */
    int len = n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1;
    for (int i = 0; i < len; i++)
        putchar(buf[i]);
    return n;
}
