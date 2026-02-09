#include <stdio.h>
#include <stdarg.h>
#include <string.h>

int fprintf(FILE* f, const char* format, ...) {
    char buf[1024];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    if (n > 0) {
        fwrite(buf, 1, (size_t)n, f);
    }
    return n;
}
