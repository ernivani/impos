#ifndef _STDIO_H
#define _STDIO_H 1

#include <sys/cdefs.h>

#define EOF (-1)

#define KEY_ESCAPE  27
#define KEY_UP      ((char)-128)
#define KEY_DOWN    ((char)-127)
#define KEY_LEFT    ((char)-126)
#define KEY_RIGHT   ((char)-125)

#ifdef __cplusplus
extern "C" {
#endif

int printf(const char* __restrict, ...);
int putchar(int);
char getchar(void);
int puts(const char*);

#ifdef __cplusplus
}
#endif

#endif