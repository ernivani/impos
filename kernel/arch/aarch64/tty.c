/*
 * aarch64 TTY driver — PL011 UART output for QEMU virt.
 *
 * On QEMU virt, the PL011 UART is mapped at 0x09000000.
 * This provides a minimal serial console for early boot and -nographic mode.
 *
 * Phase 0-1: serial-only output.  Phase 7 will add framebuffer support
 * via the same gfx_putchar_at() path that i386 uses.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/tty.h>
#include <kernel/vga.h>
#include <kernel/io.h>

/* PL011 register offsets */
#define UART0_BASE  0x09000000
#define UART_DR     0x000   /* Data register */
#define UART_FR     0x018   /* Flag register */
#define UART_IBRD   0x024   /* Integer baud rate */
#define UART_FBRD   0x028   /* Fractional baud rate */
#define UART_LCRH   0x02C   /* Line control */
#define UART_CR     0x030   /* Control */
#define UART_IMSC   0x038   /* Interrupt mask */

/* Flag register bits */
#define UART_FR_TXFF  (1 << 5)  /* TX FIFO full */
#define UART_FR_RXFE  (1 << 4)  /* RX FIFO empty */

/* Simple 80x25 virtual text grid for character-based console */
#define TTY_WIDTH  80
#define TTY_HEIGHT 25

static size_t terminal_row;
static size_t terminal_column;
static size_t win_w = TTY_WIDTH;
static size_t win_h = TTY_HEIGHT;

static inline void uart_putc(char c) {
    volatile uint32_t *uart = (volatile uint32_t *)UART0_BASE;
    /* Wait for TX FIFO to have space */
    while (uart[UART_FR / 4] & UART_FR_TXFF)
        ;
    uart[UART_DR / 4] = (uint32_t)c;
}

static inline int uart_getc(void) {
    volatile uint32_t *uart = (volatile uint32_t *)UART0_BASE;
    if (uart[UART_FR / 4] & UART_FR_RXFE)
        return -1;
    return (int)(uart[UART_DR / 4] & 0xFF);
}

void terminal_initialize(void) {
    volatile uint32_t *uart = (volatile uint32_t *)UART0_BASE;

    /* Disable UART */
    uart[UART_CR / 4] = 0;

    /* Clear all interrupts */
    uart[UART_IMSC / 4] = 0;

    /* 8 bits, no parity, 1 stop, FIFO enabled */
    uart[UART_LCRH / 4] = (3 << 5) | (1 << 4);  /* WLEN=8bit, FEN=1 */

    /* Enable UART, TX, RX */
    uart[UART_CR / 4] = (1 << 0) | (1 << 8) | (1 << 9);

    terminal_row = 0;
    terminal_column = 0;
}

void terminal_putchar(char c) {
    if (c == '\n') {
        uart_putc('\r');
        uart_putc('\n');
        terminal_column = 0;
        terminal_row++;
        if (terminal_row >= win_h)
            terminal_row = win_h - 1;
        return;
    }
    if (c == '\r') {
        uart_putc('\r');
        terminal_column = 0;
        return;
    }
    if (c == '\b') {
        if (terminal_column > 0) {
            terminal_column--;
            uart_putc('\b');
            uart_putc(' ');
            uart_putc('\b');
        }
        return;
    }
    if (c == '\t') {
        size_t next = (terminal_column + 8) & ~(size_t)7;
        if (next >= win_w) next = win_w - 1;
        while (terminal_column < next) {
            uart_putc(' ');
            terminal_column++;
        }
        return;
    }
    uart_putc(c);
    terminal_column++;
    if (terminal_column >= win_w) {
        terminal_column = 0;
        terminal_row++;
        if (terminal_row >= win_h)
            terminal_row = win_h - 1;
    }
}

void terminal_write(const char* data, size_t size) {
    for (size_t i = 0; i < size; i++)
        terminal_putchar(data[i]);
}

void terminal_writestring(const char* data) {
    terminal_write(data, strlen(data));
}

void terminal_clear(void) {
    /* ANSI clear screen + home cursor */
    const char *cls = "\033[2J\033[H";
    while (*cls) uart_putc(*cls++);
    terminal_row = 0;
    terminal_column = 0;
}

size_t terminal_get_column(void) { return terminal_column; }
size_t terminal_get_row(void) { return terminal_row; }
size_t terminal_get_width(void) { return win_w; }

void terminal_set_cursor(size_t col, size_t row) {
    if (col >= win_w) col = win_w - 1;
    if (row >= win_h) row = win_h - 1;
    terminal_column = col;
    terminal_row = row;
}

void terminal_set_window(size_t x, size_t y, size_t w, size_t h) {
    (void)x; (void)y;
    win_w = w;
    win_h = h;
    terminal_row = 0;
    terminal_column = 0;
}

void terminal_set_window_bg(uint32_t color) {
    (void)color;
}

size_t terminal_get_win_x(void) { return 0; }
size_t terminal_get_win_y(void) { return 0; }
size_t terminal_get_win_w(void) { return win_w; }
size_t terminal_get_win_h(void) { return win_h; }

/* VGA color stubs — no-ops on serial console */
void terminal_setcolor(enum vga_color fg, enum vga_color bg) {
    (void)fg; (void)bg;
}
void terminal_resetcolor(void) {}

/* Canvas stubs — not applicable until Phase 7 (framebuffer) */
void terminal_set_canvas(int win_id, uint32_t *canvas, int pw, int ph) {
    (void)win_id; (void)canvas; (void)pw; (void)ph;
}
void terminal_clear_canvas(void) {}
void terminal_notify_canvas_resize(int win_id, uint32_t *canvas, int pw, int ph) {
    (void)win_id; (void)canvas; (void)pw; (void)ph;
}
