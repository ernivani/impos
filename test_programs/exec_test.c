#include <stdint.h>

static inline int32_t sys_write(int fd, const void *buf, uint32_t len) {
    int32_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(4), "b"(fd), "c"(buf), "d"(len));
    return ret;
}

static inline int32_t sys_getpid(void) {
    int32_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(20));
    return ret;
}

static inline int32_t sys_execve(const char *path, const char **argv, const char **envp) {
    int32_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(11), "b"(path), "c"(argv), "d"(envp));
    return ret;
}

static inline void sys_exit(int code) {
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    __builtin_unreachable();
}

static void print_int(int fd, int val) {
    char buf[12];
    int i = 0;
    if (val == 0) {
        sys_write(fd, "0", 1);
        return;
    }
    if (val < 0) {
        sys_write(fd, "-", 1);
        val = -val;
    }
    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    /* reverse */
    for (int j = 0; j < i / 2; j++) {
        char tmp = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = tmp;
    }
    sys_write(fd, buf, i);
}

void _start(void) {
    int pid = sys_getpid();
    sys_write(1, "EXEC_PID=", 9);
    print_int(1, pid);
    sys_write(1, "\n", 1);

    const char *argv[] = { "/bin/exec_target", (void*)0 };
    sys_execve("/bin/exec_target", argv, (void*)0);

    /* Should not reach here */
    sys_write(1, "EXEC_FAIL\n", 10);
    sys_exit(1);
}
