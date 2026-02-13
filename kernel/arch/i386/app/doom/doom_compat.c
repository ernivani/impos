/* doom_compat.c — ImposOS compatibility stubs for doomgeneric */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

char *getenv(const char *name) {
    (void)name;
    return (char *)0;
}

int remove(const char *path) {
    (void)path;
    return -1;
}

int rename(const char *old_name, const char *new_name) {
    (void)old_name;
    (void)new_name;
    return -1;
}

double atof(const char *str) {
    /* Simple integer-only implementation (doom configs are mostly ints) */
    int neg = 0;
    double result = 0.0;

    while (*str == ' ' || *str == '\t') str++;
    if (*str == '-') { neg = 1; str++; }
    else if (*str == '+') { str++; }

    while (*str >= '0' && *str <= '9') {
        result = result * 10.0 + (*str - '0');
        str++;
    }

    if (*str == '.') {
        str++;
        double frac = 0.1;
        while (*str >= '0' && *str <= '9') {
            result += (*str - '0') * frac;
            frac *= 0.1;
            str++;
        }
    }

    return neg ? -result : result;
}

int vfprintf(FILE *f, const char *fmt, va_list ap) {
    (void)f;
    char buf[512];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    printf("%s", buf);
    return n;
}
