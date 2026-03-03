#ifndef _KERNEL_SYSCALL_H
#define _KERNEL_SYSCALL_H

#include <kernel/idt.h>

/*
 * Syscall numbers (shared across architectures).
 *
 * i386:    INT 0x80, EAX=num, args=EBX/ECX/EDX/ESI/EDI/EBP, ret=EAX
 * aarch64: SVC #0,   x8=num,  args=x0/x1/x2/x3/x4/x5,      ret=x0
 */
#define SYS_EXIT    0
#define SYS_YIELD   1
#define SYS_SLEEP   2   /* arg0=ms */
#define SYS_GETPID  3
#define SYS_READ    4   /* arg0=fd, arg1=buf, arg2=count → bytes_read */
#define SYS_WRITE   5   /* arg0=fd, arg1=buf, arg2=count → bytes_written */
#define SYS_OPEN    6   /* arg0=path, arg1=flags */
#define SYS_CLOSE   7   /* arg0=fd */
#define SYS_PIPE    8   /* arg0=&int[2] → fills [read_fd, write_fd] */
#define SYS_KILL    9   /* arg0=pid, arg1=signal → 0 or -1 */
#define SYS_SIGACTION  10  /* arg0=signum, arg1=handler → old_handler */
#define SYS_SIGRETURN  11  /* restore saved context from signal handler */
#define SYS_SHM_CREATE 12  /* arg0=name, arg1=size → region_id */
#define SYS_SHM_ATTACH 13  /* arg0=region_id → virt_addr */
#define SYS_SHM_DETACH 14  /* arg0=region_id → 0 or -1 */
#define SYS_IOCTL      15  /* arg0=fd, arg1=cmd, arg2=arg → 0 or -errno */
#define SYS_MMAP       16  /* arg0=addr, arg1=len, arg2=prot, arg3=flags → addr or -1 */
#define SYS_WAITPID    17  /* arg0=pid, arg1=&wstatus, arg2=options → child_pid or -1 */
#define SYS_NICE       18  /* arg0=priority (0-3) → 0 or -1 */

/* Syscall dispatcher — called from exception handler (INT 0x80 / SVC #0).
 * Returns (possibly different) stack pointer after scheduling. */
registers_t* syscall_handler(registers_t* regs);

#endif
