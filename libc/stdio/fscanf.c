#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* Internal sscanf/fscanf parser */
static int scan_internal(const char* str, int (*getc_fn)(void* ctx), void* ctx,
                          const char* format, va_list args) {
    int matched = 0;
    const char* p = format;
    const char* s = str; /* used when str != NULL */

    while (*p) {
        /* Skip whitespace in format */
        if (*p == ' ' || *p == '\t' || *p == '\n') {
            if (str) {
                while (*s == ' ' || *s == '\t' || *s == '\n') s++;
            } else {
                int c;
                while ((c = getc_fn(ctx)) != EOF) {
                    if (c != ' ' && c != '\t' && c != '\n') {
                        /* Can't unget, but for simplicity we accept this */
                        break;
                    }
                }
            }
            p++;
            continue;
        }

        if (*p != '%') {
            /* Literal match */
            int c;
            if (str) {
                c = *s ? (unsigned char)*s++ : EOF;
            } else {
                c = getc_fn(ctx);
            }
            if (c != (unsigned char)*p) return matched;
            p++;
            continue;
        }

        p++; /* skip '%' */

        if (*p == '%') {
            /* Literal % */
            int c;
            if (str) {
                c = *s ? (unsigned char)*s++ : EOF;
            } else {
                c = getc_fn(ctx);
            }
            if (c != '%') return matched;
            p++;
            continue;
        }

        if (*p == 'n') {
            /* %n - number of chars consumed so far */
            int* np = va_arg(args, int*);
            if (np) *np = (int)(str ? (s - str) : 0);
            p++;
            continue;
        }

        /* Skip leading whitespace for %d, %u, %x, %s */
        if (str) {
            while (*s == ' ' || *s == '\t') s++;
        }

        if (*p == 'd') {
            int* ip = va_arg(args, int*);
            int val = 0;
            int neg = 0;

            const char* start = s;
            if (str) {
                if (*s == '-') { neg = 1; s++; }
                else if (*s == '+') { s++; }
                if (*s < '0' || *s > '9') return matched;
                while (*s >= '0' && *s <= '9')
                    val = val * 10 + (*s++ - '0');
            }
            if (neg) val = -val;
            if (ip) *ip = val;
            if (s == start) return matched;
            matched++;
            p++;
        } else if (*p == 'u') {
            unsigned int* up = va_arg(args, unsigned int*);
            unsigned int val = 0;

            if (str) {
                if (*s < '0' || *s > '9') return matched;
                while (*s >= '0' && *s <= '9')
                    val = val * 10 + (*s++ - '0');
            }
            if (up) *up = val;
            matched++;
            p++;
        } else if (*p == 'x') {
            unsigned int* xp = va_arg(args, unsigned int*);
            unsigned int val = 0;

            if (str) {
                /* Skip optional 0x prefix */
                if (*s == '0' && (*(s+1) == 'x' || *(s+1) == 'X'))
                    s += 2;
                const char* start = s;
                while (1) {
                    if (*s >= '0' && *s <= '9')
                        val = val * 16 + (*s - '0');
                    else if (*s >= 'a' && *s <= 'f')
                        val = val * 16 + (*s - 'a' + 10);
                    else if (*s >= 'A' && *s <= 'F')
                        val = val * 16 + (*s - 'A' + 10);
                    else
                        break;
                    s++;
                }
                if (s == start) return matched;
            }
            if (xp) *xp = val;
            matched++;
            p++;
        } else if (*p == 's') {
            char* sp = va_arg(args, char*);
            int i = 0;

            if (str) {
                while (*s && *s != ' ' && *s != '\t' && *s != '\n') {
                    if (sp) sp[i] = *s;
                    i++;
                    s++;
                }
            }
            if (sp) sp[i] = '\0';
            if (i == 0) return matched;
            matched++;
            p++;
        } else if (*p == 'c') {
            char* cp = va_arg(args, char*);
            int c;
            if (str) {
                c = *s ? (unsigned char)*s++ : EOF;
            } else {
                c = getc_fn(ctx);
            }
            if (c == EOF) return matched;
            if (cp) *cp = (char)c;
            matched++;
            p++;
        } else {
            /* Unknown format specifier, stop */
            return matched;
        }
    }

    return matched;
}

int sscanf(const char* str, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int result = scan_internal(str, NULL, NULL, format, args);
    va_end(args);
    return result;
}

static int file_getc_wrapper(void* ctx) {
    return fgetc((FILE*)ctx);
}

int fscanf(FILE* f, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int result = scan_internal(NULL, file_getc_wrapper, f, format, args);
    va_end(args);
    return result;
}
