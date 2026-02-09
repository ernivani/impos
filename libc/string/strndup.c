#include <string.h>
#include <stdlib.h>

char* strndup(const char* s, size_t n) {
    size_t len = strnlen(s, n);
    char* dup = (char*)malloc(len + 1);
    if (!dup)
        return NULL;
    memcpy(dup, s, len);
    dup[len] = '\0';
    return dup;
}
