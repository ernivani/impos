#ifndef _SETJMP_H
#define _SETJMP_H 1

#include <sys/cdefs.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	unsigned int ebx;
	unsigned int esi;
	unsigned int edi;
	unsigned int ebp;
	unsigned int esp;
	unsigned int eip;
} jmp_buf[1];

int setjmp(jmp_buf env);
__attribute__((__noreturn__))
void longjmp(jmp_buf env, int val);

#ifdef __cplusplus
}
#endif

#endif
