#ifndef _KERNEL_IO_H
#define _KERNEL_IO_H

#include <stdint.h>

/* ═══════════════════════════════════════════════════════════════════
 * Architecture-specific I/O primitives
 *
 * i386:    x86 port I/O (in/out instructions) + COM1 serial
 * aarch64: MMIO (volatile pointer access) + PL011 UART serial
 * ═══════════════════════════════════════════════════════════════════ */

#if defined(__aarch64__)

/* ── MMIO access (ARM uses memory-mapped I/O, no port I/O) ──────── */

static inline void mmio_write32(uintptr_t addr, uint32_t value) {
    *(volatile uint32_t *)addr = value;
}

static inline uint32_t mmio_read32(uintptr_t addr) {
    return *(volatile uint32_t *)addr;
}

static inline void mmio_write16(uintptr_t addr, uint16_t value) {
    *(volatile uint16_t *)addr = value;
}

static inline uint16_t mmio_read16(uintptr_t addr) {
    return *(volatile uint16_t *)addr;
}

static inline void mmio_write8(uintptr_t addr, uint8_t value) {
    *(volatile uint8_t *)addr = value;
}

static inline uint8_t mmio_read8(uintptr_t addr) {
    return *(volatile uint8_t *)addr;
}

/* x86 port I/O stubs — compile error if any i386 driver code
   accidentally gets compiled for aarch64. */
#define outb(port, val)  _Static_assert(0, "outb: x86 port I/O not available on aarch64")
#define inb(port)        _Static_assert(0, "inb: x86 port I/O not available on aarch64")
#define outw(port, val)  _Static_assert(0, "outw: x86 port I/O not available on aarch64")
#define inw(port)        _Static_assert(0, "inw: x86 port I/O not available on aarch64")
#define outl(port, val)  _Static_assert(0, "outl: x86 port I/O not available on aarch64")
#define inl(port)        _Static_assert(0, "inl: x86 port I/O not available on aarch64")
#define insw(p,a,c)      _Static_assert(0, "insw: x86 port I/O not available on aarch64")
#define outsw(p,a,c)     _Static_assert(0, "outsw: x86 port I/O not available on aarch64")

static inline void io_wait(void) {
    /* No port 0x80 trick on ARM — a data synchronization barrier suffices */
    __asm__ volatile("dsb sy" ::: "memory");
}

/* ── Interrupt control (DAIF flags) ─────────────────────────────── */

static inline void cli(void) {
    __asm__ volatile("msr daifset, #2" ::: "memory");  /* Mask IRQ */
}

static inline void sti(void) {
    __asm__ volatile("msr daifclr, #2" ::: "memory");  /* Unmask IRQ */
}

static inline uint32_t irq_save(void) {
    uint64_t daif;
    __asm__ volatile("mrs %0, daif" : "=r"(daif));
    __asm__ volatile("msr daifset, #2" ::: "memory");
    return (uint32_t)daif;
}

static inline void irq_restore(uint32_t flags) {
    __asm__ volatile("msr daif, %0" :: "r"((uint64_t)flags) : "memory");
}

/* ── Serial debug output (PL011 UART at 0x09000000) ────────────── */
#define PL011_BASE    0x09000000
#define PL011_DR      (PL011_BASE + 0x000)
#define PL011_FR      (PL011_BASE + 0x018)
#define PL011_FR_TXFF (1 << 5)
#define PL011_FR_RXFE (1 << 4)

static inline void serial_init(void) {
    /* PL011 on QEMU virt is pre-configured — no baud rate setup needed */
}

static inline void serial_putc(char c) {
    while (mmio_read32(PL011_FR) & PL011_FR_TXFF)
        ;
    mmio_write32(PL011_DR, (uint32_t)c);
}

static inline void serial_puts(const char *s) {
    while (*s) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s++);
    }
}

static inline int serial_data_ready(void) {
    return !(mmio_read32(PL011_FR) & PL011_FR_RXFE);
}

static inline char serial_getc(void) {
    while (!serial_data_ready());
    return (char)(mmio_read32(PL011_DR) & 0xFF);
}

#else /* i386 */

/* ── x86 Port I/O ──────────────────────────────────────────────── */

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

/* ── Interrupt control ──────────────────────────────────────────── */

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

static inline int serial_data_ready(void) {
    return inb(SERIAL_COM1 + 5) & 0x01;
}

static inline char serial_getc(void) {
    while (!serial_data_ready());
    return (char)inb(SERIAL_COM1);
}

#endif /* __aarch64__ vs i386 */

/* ── Shared serial printf (arch-independent) ────────────────────── */

/* Minimal serial printf — supports %s, %d, %x, %u, %p, %l (long) */
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

/* Boot-time profiler: prints [seconds.milliseconds] prefix to serial.
   On i386 the PIT runs at 120Hz; on aarch64 the generic timer is used.
   Before timer init, ticks=0. */
extern uint32_t pit_get_ticks(void);
#define TIME(fmt, ...) do { \
    uint32_t _t = pit_get_ticks(); \
    uint32_t _s = _t / 120; \
    uint32_t _ms = (_t % 120) * 1000 / 120; \
    serial_printf("[%u.%u%u%u] " fmt "\n", \
                  _s, _ms / 100, (_ms / 10) % 10, _ms % 10, \
                  ##__VA_ARGS__); \
} while(0)

#endif /* _KERNEL_IO_H */
