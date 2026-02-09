#ifndef _STDIO_H
#define _STDIO_H 1

#include <sys/cdefs.h>
#include <stddef.h>
#include <stdint.h>

#define EOF (-1)

/* Special key codes - using CP437 box-drawing range to avoid
   conflicts with French accented characters (0x80-0x9F range) */
#define KEY_ESCAPE  27
#define KEY_UP      ((char)0xB0)
#define KEY_DOWN    ((char)0xB1)
#define KEY_LEFT    ((char)0xB2)
#define KEY_RIGHT   ((char)0xB3)
#define KEY_HOME    ((char)0xB4)
#define KEY_END     ((char)0xB5)
#define KEY_PGUP    ((char)0xB6)
#define KEY_PGDN    ((char)0xB7)
#define KEY_DEL     ((char)0xB8)
#define KEY_INS     ((char)0xB9)

/* Keyboard layouts */
#define KB_LAYOUT_FR  0
#define KB_LAYOUT_US  1

/* FILE type */
typedef struct _FILE FILE;

extern FILE* stdin;
extern FILE* stdout;
extern FILE* stderr;

#ifdef __cplusplus
extern "C" {
#endif

int printf(const char* __restrict, ...);
int snprintf(char* __restrict, size_t, const char* __restrict, ...);
int sprintf(char* __restrict, const char* __restrict, ...);
int vsnprintf(char* __restrict, size_t, const char* __restrict, __builtin_va_list);
int putchar(int);
char getchar(void);
int puts(const char*);
void keyboard_set_layout(int layout);
int  keyboard_get_layout(void);

/* FILE I/O */
FILE* fopen(const char* path, const char* mode);
int   fclose(FILE* f);
int   fgetc(FILE* f);
int   fputc(int c, FILE* f);
size_t fread(void* ptr, size_t size, size_t count, FILE* f);
size_t fwrite(const void* ptr, size_t size, size_t count, FILE* f);
int   fflush(FILE* f);
int   feof(FILE* f);
int   ferror(FILE* f);
int   fputs(const char* s, FILE* f);
char* fgets(char* s, int size, FILE* f);

/* Formatted I/O */
int fprintf(FILE* f, const char* format, ...);
int sscanf(const char* str, const char* format, ...);
int fscanf(FILE* f, const char* format, ...);

#ifdef __cplusplus
}
#endif

#endif
