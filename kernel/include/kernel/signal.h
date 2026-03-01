#ifndef _KERNEL_SIGNAL_H
#define _KERNEL_SIGNAL_H

#include <stdint.h>
#include <kernel/idt.h>

/* Signal numbers (POSIX compatible) */
#define SIGINT    2
#define SIGILL    4   /* Invalid opcode */
#define SIGBUS    7   /* Bus error (double fault, alignment) */
#define SIGFPE    8   /* Arithmetic exception (div by zero) */
#define SIGKILL   9
#define SIGUSR1  10
#define SIGSEGV  11   /* Segmentation fault (page fault, GPF) */
#define SIGUSR2  12
#define SIGPIPE  13
#define SIGALRM  14
#define SIGTERM  15
#define SIGCHLD  17
#define SIGCONT  18
#define SIGSTOP  19
#define SIGTSTP  20
#define SIGTTIN  21
#define SIGTTOU  22
#define NSIG     32

typedef void (*sig_handler_t)(int);

#define SIG_DFL  ((sig_handler_t)0)
#define SIG_IGN  ((sig_handler_t)1)

/* sigaction flags */
#define SA_SIGINFO    4
#define SA_RESTART    0x10000000

/* sigprocmask how values */
#define SIG_BLOCK     0
#define SIG_UNBLOCK   1
#define SIG_SETMASK   2

/* Saved user-mode context pushed onto user stack during delivery */
typedef struct {
    uint32_t eip, cs, eflags, esp, ss;
    uint32_t eax, ecx, edx, ebx, esi, edi, ebp;
    uint32_t ds, es, fs, gs;
} sig_context_t;  /* 16 x 4 = 64 bytes */

/* Per-task signal state (embedded in task_info_t) */
typedef struct {
    sig_handler_t handlers[NSIG];
    uint32_t      pending;      /* bitmask of pending signals */
    uint32_t      blocked;      /* bitmask of blocked signals */
    int           in_handler;   /* 1 = currently in handler, no nesting */
    uint32_t      alarm_ticks;  /* PIT ticks until SIGALRM fires, 0=disabled */
} sig_state_t;

void          sig_init(sig_state_t *ss);
int           sig_send(int tid, int signum);
int           sig_send_pid(int pid, int signum);
sig_handler_t sig_set_handler(int tid, int signum, sig_handler_t handler);
int           sig_deliver(int tid, registers_t *regs);
void          sig_check_alarms(void);  /* called from PIT handler each tick */
int           sig_sigprocmask(int tid, int how, uint32_t set, uint32_t *oldset);

/* Trampoline symbol (defined in signal.c via top-level asm) */
extern void _sig_trampoline(void);

#endif
