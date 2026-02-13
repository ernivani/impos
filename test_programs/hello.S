/* Minimal Linux i386 hello world — uses write() and exit_group() syscalls.
 * Linked at 0x08048000 (standard Linux i386 load address).
 * Tests: ELF loader, INT 0x80 routing, write syscall, exit_group syscall. */

.section .text
.global _start
_start:
    /* write(1, msg, 14) */
    movl $4, %eax          /* Linux sys_write */
    movl $1, %ebx          /* fd = stdout */
    leal msg, %ecx         /* buf = &msg */
    movl $14, %edx         /* count = 14 */
    int  $0x80

    /* exit_group(0) */
    movl $252, %eax        /* Linux sys_exit_group */
    xorl %ebx, %ebx       /* status = 0 */
    int  $0x80

.section .rodata
msg: .ascii "Hello, world!\n"
