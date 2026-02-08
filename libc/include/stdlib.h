#ifndef _STDLIB_H
#define _STDLIB_H 1

#include <sys/cdefs.h>
#include <setjmp.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#ifdef __cplusplus
extern "C" {
#endif

__attribute__((__noreturn__))
void abort(void);

__attribute__((__noreturn__))
void exit(int status);

int atexit(void (*function)(void));

void exit_set_restart_point(jmp_buf *env);

#ifdef __cplusplus
}
#endif

#endif
