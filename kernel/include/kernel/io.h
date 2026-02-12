#ifndef _KERNEL_IO_H
#define _KERNEL_IO_H

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outl(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void insw(uint16_t port, void* addr, uint32_t count) {
    __asm__ volatile ("rep insw" : "+D"(addr), "+c"(count) : "d"(port) : "memory");
}

static inline void outsw(uint16_t port, const void* addr, uint32_t count) {
    __asm__ volatile ("rep outsw" : "+S"(addr), "+c"(count) : "d"(port) : "memory");
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

/* Interrupt control for preemptive multitasking */
static inline void cli(void) { __asm__ volatile("cli"); }
static inline void sti(void) { __asm__ volatile("sti"); }

static inline uint32_t irq_save(void) {
    uint32_t flags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(flags));
    return flags;
}

static inline void irq_restore(uint32_t flags) {
    __asm__ volatile("push %0; popf" : : "r"(flags));
}

/* ── Serial debug output (COM1 0x3F8) ─────────────────────────── */
#define SERIAL_COM1 0x3F8

static inline void serial_init(void) {
    outb(SERIAL_COM1 + 1, 0x00);   /* Disable interrupts */
    outb(SERIAL_COM1 + 3, 0x80);   /* Enable DLAB */
    outb(SERIAL_COM1 + 0, 0x01);   /* 115200 baud */
    outb(SERIAL_COM1 + 1, 0x00);
    outb(SERIAL_COM1 + 3, 0x03);   /* 8N1 */
    outb(SERIAL_COM1 + 2, 0xC7);   /* Enable FIFO */
    outb(SERIAL_COM1 + 4, 0x0B);   /* IRQs enabled, RTS/DSR set */
}

static inline void serial_putc(char c) {
    while (!(inb(SERIAL_COM1 + 5) & 0x20));
    outb(SERIAL_COM1, (uint8_t)c);
}

static inline void serial_puts(const char *s) {
    while (*s) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s++);
    }
}

/* Minimal serial printf — supports %s, %d, %x, %u, %p */
static inline void serial_printf(const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            if (*fmt == '\n') serial_putc('\r');
            serial_putc(*fmt);
            continue;
        }
        fmt++;
        if (*fmt == 's') {
            const char *s = __builtin_va_arg(ap, const char *);
            serial_puts(s ? s : "(null)");
        } else if (*fmt == 'd') {
            int v = __builtin_va_arg(ap, int);
            if (v < 0) { serial_putc('-'); v = -v; }
            char buf[12]; int i = 0;
            do { buf[i++] = '0' + v % 10; v /= 10; } while (v);
            while (i--) serial_putc(buf[i]);
        } else if (*fmt == 'u') {
            unsigned v = __builtin_va_arg(ap, unsigned);
            char buf[12]; int i = 0;
            do { buf[i++] = '0' + v % 10; v /= 10; } while (v);
            while (i--) serial_putc(buf[i]);
        } else if (*fmt == 'x' || *fmt == 'p') {
            if (*fmt == 'p') serial_puts("0x");
            unsigned v = __builtin_va_arg(ap, unsigned);
            char buf[9]; int i = 0;
            do { int d = v & 0xF; buf[i++] = d < 10 ? '0'+d : 'a'+d-10; v >>= 4; } while (v);
            while (i--) serial_putc(buf[i]);
        } else if (*fmt == '%') {
            serial_putc('%');
        }
    }
    __builtin_va_end(ap);
}

#define DBG(fmt, ...) serial_printf("[DBG] " fmt "\n", ##__VA_ARGS__)

#endif
