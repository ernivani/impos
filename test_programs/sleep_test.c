#include <stdint.h>

static inline int32_t sys_write(int fd, const void *buf, uint32_t len) {
    int32_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(4), "b"(fd), "c"(buf), "d"(len));
    return ret;
}

static inline int32_t sys_nanosleep(const void *req, void *rem) {
    int32_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(162), "b"(req), "c"(rem));
    return ret;
}

static inline void sys_exit(int code) {
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    __builtin_unreachable();
}

struct timespec { int32_t tv_sec; int32_t tv_nsec; };

void _start(void) {
    sys_write(1, "SLEEP_START\n", 12);
    struct timespec ts = { .tv_sec = 1, .tv_nsec = 0 };
    sys_nanosleep(&ts, (void*)0);
    sys_write(1, "SLEEP_DONE\n", 11);
    sys_exit(0);
}
