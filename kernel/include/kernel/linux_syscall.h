#ifndef _KERNEL_LINUX_SYSCALL_H
#define _KERNEL_LINUX_SYSCALL_H

#include <kernel/idt.h>

/* Linux i386 syscall numbers (subset — enough for static musl hello world) */
#define LINUX_SYS_exit            1
#define LINUX_SYS_read            3
#define LINUX_SYS_write           4
#define LINUX_SYS_brk             45
#define LINUX_SYS_ioctl           54
#define LINUX_SYS_writev          146
#define LINUX_SYS_mmap2           192
#define LINUX_SYS_set_thread_area 243
#define LINUX_SYS_exit_group      252

/* Linux errno values */
#define LINUX_ENOSYS  38
#define LINUX_ENOMEM  12
#define LINUX_EBADF    9
#define LINUX_EINVAL  22

/* Linux mmap flags */
#define LINUX_MAP_ANONYMOUS  0x20

/* Linux iovec for writev */
struct linux_iovec {
    uint32_t iov_base;
    uint32_t iov_len;
};

/* Linux user_desc for set_thread_area */
struct linux_user_desc {
    uint32_t entry_number;
    uint32_t base_addr;
    uint32_t limit;
    uint32_t seg_32bit       : 1;
    uint32_t contents        : 2;
    uint32_t read_exec_only  : 1;
    uint32_t limit_in_pages  : 1;
    uint32_t seg_not_present : 1;
    uint32_t useable         : 1;
};

/* Dispatch a Linux i386 syscall. Called from syscall_handler for ELF tasks. */
registers_t* linux_syscall_handler(registers_t* regs);

#endif
