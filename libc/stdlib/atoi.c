#include <stdlib.h>

int atoi(const char* str) {
    int result = 0;
    int sign = 1;

    /* Skip whitespace */
    while (*str == ' ' || *str == '\t' || *str == '\n' ||
           *str == '\r' || *str == '\f' || *str == '\v') {
        str++;
    }

    /* Optional sign */
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }

    /* Accumulate digits */
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }

    return sign * result;
}
