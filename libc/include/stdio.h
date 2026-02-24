#ifndef _STDIO_H
#define _STDIO_H 1

#include <sys/cdefs.h>
#include <stddef.h>
#include <stdint.h>

#define EOF (-1)

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

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
#define KEY_ALT_TAB ((char)0xBA)
#define KEY_SUPER   ((char)0xBB)
#define KEY_FINDER  ((char)0xBC)

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
void keyboard_push_scancode(uint8_t scancode);
void keyboard_set_idle_callback(void (*cb)(void));
int  keyboard_force_exit(void);
void keyboard_request_force_exit(void);
int  keyboard_data_available(void);
int  keyboard_getchar_nb(void);     /* non-blocking: returns char or 0 */
int  keyboard_check_double_ctrl(void);
void keyboard_run_idle(void);
int  keyboard_get_shift(void);
int  keyboard_get_ctrl(void);
int  keyboard_get_alt(void);
int  keyboard_get_raw_scancode(void);  /* raw PS/2 scancode or -1 */

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

/* Seeking */
int   fseek(FILE* f, long offset, int whence);
long  ftell(FILE* f);
void  rewind(FILE* f);
int   ungetc(int c, FILE* f);

/* Formatted I/O */
int fprintf(FILE* f, const char* format, ...);
int sscanf(const char* str, const char* format, ...);
int fscanf(FILE* f, const char* format, ...);

/* File operations */
int remove(const char* path);
int rename(const char* oldpath, const char* newpath);

#ifdef __cplusplus
}
#endif

#endif
