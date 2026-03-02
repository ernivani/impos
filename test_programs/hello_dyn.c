/* hello_dyn.c — minimal dynamically-linked test program for ImposOS
 * Linked against musl libc.so / ld-musl-i386.so.1
 * Tests the full ldso bootstrap: kernel loads interpreter → interpreter
 * resolves symbols → CRT calls __libc_start_main → calls main().
 * musl uses int 0x80 for i386 syscalls (matches our linux_syscall.c).
 */

/* write() prototype — resolved from libc.so at runtime */
long write(int fd, const void *buf, unsigned long count);
void _exit(int status);

int main(void) {
    const char msg[] = "Hello from dynamic!\n";
    write(1, msg, sizeof(msg) - 1);
    _exit(0);
    return 0;
}
