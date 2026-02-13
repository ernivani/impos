#include <strings.h>

int strcasecmp(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        int c1 = (unsigned char)*s1;
        int c2 = (unsigned char)*s2;
        if (c1 >= 'A' && c1 <= 'Z') c1 += 'a' - 'A';
        if (c2 >= 'A' && c2 <= 'Z') c2 += 'a' - 'A';
        if (c1 != c2) return c1 - c2;
        s1++;
        s2++;
    }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

int strncasecmp(const char *s1, const char *s2, size_t n) {
    while (n-- && *s1 && *s2) {
        int c1 = (unsigned char)*s1;
        int c2 = (unsigned char)*s2;
        if (c1 >= 'A' && c1 <= 'Z') c1 += 'a' - 'A';
        if (c2 >= 'A' && c2 <= 'Z') c2 += 'a' - 'A';
        if (c1 != c2) return c1 - c2;
        s1++;
        s2++;
    }
    if (n == (size_t)-1) return 0;
    return (unsigned char)*s1 - (unsigned char)*s2;
}
