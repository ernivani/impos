#include <string.h>

char* strstr(const char* haystack, const char* needle) {
    if (!*needle) return (char*)haystack;

    size_t needle_len = strlen(needle);

    for (; *haystack; haystack++) {
        const char* h = haystack;
        const char* n = needle;
        size_t i = 0;

        while (i < needle_len && *h && *h == *n) {
            h++;
            n++;
            i++;
        }

        if (i == needle_len) {
            return (char*)haystack;
        }
    }

    return NULL;
}
