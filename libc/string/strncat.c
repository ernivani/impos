#include <string.h>

char* strncat(char* dst, const char* src, size_t n) {
    char* d = dst;
    while (*d) d++;
    while (n-- > 0 && *src) {
        *d++ = *src++;
    }
    *d = '\0';
    return dst;
}
