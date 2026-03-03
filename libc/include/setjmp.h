#ifndef _SETJMP_H
#define _SETJMP_H 1

#include <sys/cdefs.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__aarch64__)
/* aarch64: x19-x30 (12 regs) + SP = 13 × uint64_t = 104 bytes */
typedef struct {
	unsigned long regs[13];
} jmp_buf[1];
#else
/* i386: ebx, esi, edi, ebp, esp, eip = 6 × uint32_t = 24 bytes */
typedef struct {
	unsigned int ebx;
	unsigned int esi;
	unsigned int edi;
	unsigned int ebp;
	unsigned int esp;
	unsigned int eip;
} jmp_buf[1];
#endif

int setjmp(jmp_buf env);
__attribute__((__noreturn__))
void longjmp(jmp_buf env, int val);

#ifdef __cplusplus
}
#endif

#endif
