#ifndef _KERNEL_SYSCALL_H
#define _KERNEL_SYSCALL_H

#include <kernel/idt.h>

/* Syscall numbers (EAX) */
#define SYS_EXIT    0
#define SYS_YIELD   1
#define SYS_SLEEP   2
#define SYS_GETPID  3
#define SYS_READ    4   /* EBX=fd, ECX=buf, EDX=count → EAX=bytes_read */
#define SYS_WRITE   5   /* EBX=fd, ECX=buf, EDX=count → EAX=bytes_written */
#define SYS_OPEN    6   /* reserved for future file FDs */
#define SYS_CLOSE   7   /* EBX=fd */
#define SYS_PIPE    8   /* EBX=&int[2] → fills [read_fd, write_fd] */
#define SYS_KILL    9   /* EBX=pid, ECX=signal → EAX=0 or -1 */
#define SYS_SIGACTION  10  /* EBX=signum, ECX=handler → EAX=old_handler */
#define SYS_SIGRETURN  11  /* restore saved context from signal handler */
#define SYS_SHM_CREATE 12  /* EBX=name, ECX=size → EAX=region_id */
#define SYS_SHM_ATTACH 13  /* EBX=region_id → EAX=virt_addr */
#define SYS_SHM_DETACH 14  /* EBX=region_id → EAX=0 or -1 */

/* Syscall dispatcher — called from isr_handler for INT 0x80.
 * Returns (possibly different) stack pointer after scheduling. */
registers_t* syscall_handler(registers_t* regs);

#endif
