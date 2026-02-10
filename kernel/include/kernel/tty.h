#ifndef _KERNEL_TTY_H
#define _KERNEL_TTY_H

#include <stddef.h>
#include <kernel/vga.h>

void terminal_initialize(void);
void terminal_putchar(char c);
void terminal_write(const char* data, size_t size);
void terminal_writestring(const char* data);
void terminal_clear(void);
size_t terminal_get_column(void);
size_t terminal_get_row(void);
size_t terminal_get_width(void);
void terminal_set_cursor(size_t col, size_t row);

/* Window region (for desktop windowed mode) */
void terminal_set_window(size_t x, size_t y, size_t w, size_t h);
size_t terminal_get_win_x(void);
size_t terminal_get_win_y(void);
size_t terminal_get_win_w(void);
size_t terminal_get_win_h(void);
void terminal_set_window_bg(uint32_t color);

/* Canvas mode: terminal draws into a WM canvas buffer */
void terminal_set_canvas(int win_id, uint32_t *canvas, int pw, int ph);
void terminal_clear_canvas(void);

/* Color functions */
void terminal_setcolor(enum vga_color fg, enum vga_color bg);
void terminal_resetcolor(void);

#endif