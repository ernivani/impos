#ifndef _KERNEL_SYSCALL_H
#define _KERNEL_SYSCALL_H

#include <kernel/idt.h>

/* Syscall numbers (EAX) */
#define SYS_EXIT    0
#define SYS_YIELD   1
#define SYS_SLEEP   2
#define SYS_GETPID  3

/* Syscall dispatcher — called from isr_handler for INT 0x80.
 * Returns (possibly different) stack pointer after scheduling. */
registers_t* syscall_handler(registers_t* regs);

#endif
