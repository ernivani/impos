#include <string.h>


char* strchr(const char* str, int c) {
    while (*str != (char)c) {
        if (!*str++) {
            return NULL;
        }
    }
    return (char*)str;
}
