#ifndef _KERNEL_SIGNAL_H
#define _KERNEL_SIGNAL_H

#include <stdint.h>
#include <kernel/idt.h>

/* Signal numbers */
#define SIGINT    2
#define SIGKILL   9
#define SIGUSR1  10
#define SIGUSR2  12
#define SIGPIPE  13
#define SIGILL    4   /* Invalid opcode */
#define SIGBUS    7   /* Bus error (double fault, alignment) */
#define SIGFPE    8   /* Arithmetic exception (div by zero) */
#define SIGSEGV  11   /* Segmentation fault (page fault, GPF) */
#define SIGTERM  15
#define NSIG     16

typedef void (*sig_handler_t)(int);

#define SIG_DFL  ((sig_handler_t)0)
#define SIG_IGN  ((sig_handler_t)1)

/* Saved user-mode context pushed onto user stack during delivery */
typedef struct {
    uint32_t eip, cs, eflags, esp, ss;
    uint32_t eax, ecx, edx, ebx, esi, edi, ebp;
    uint32_t ds, es, fs, gs;
} sig_context_t;  /* 16 x 4 = 64 bytes */

/* Per-task signal state (embedded in task_info_t) */
typedef struct {
    sig_handler_t handlers[NSIG];
    uint32_t      pending;      /* bitmask */
    int           in_handler;   /* 1 = currently in handler, no nesting */
} sig_state_t;

void          sig_init(sig_state_t *ss);
int           sig_send(int tid, int signum);
int           sig_send_pid(int pid, int signum);
sig_handler_t sig_set_handler(int tid, int signum, sig_handler_t handler);
int           sig_deliver(int tid, registers_t *regs);

/* Trampoline symbol (defined in signal.c via top-level asm) */
extern void _sig_trampoline(void);

#endif
