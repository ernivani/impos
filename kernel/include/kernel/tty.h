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
void terminal_set_cursor(size_t col, size_t row);

/* Color functions */
void terminal_setcolor(enum vga_color fg, enum vga_color bg);
void terminal_resetcolor(void);

#endif