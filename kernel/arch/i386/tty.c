#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/tty.h>
#include <kernel/gfx.h>
#include <kernel/io.h>
#include <kernel/wm.h>

#include "vga.h"

static size_t VGA_WIDTH = 80;
static size_t VGA_HEIGHT = 25;
static uint16_t* const VGA_MEMORY = (uint16_t*) 0xB8000;

static size_t terminal_row;
static size_t terminal_column;
static uint8_t terminal_color;
static uint16_t* terminal_buffer;
static int gfx_mode = 0;

/* Window region (character grid units) — defaults to fullscreen */
static size_t win_x = 0;
static size_t win_y = 0;
static size_t win_w = 80;
static size_t win_h = 25;
/* Custom background color for windowed mode (0 = use VGA color) */
static uint32_t win_bg_color = 0;
static int win_bg_set = 0;

/* Canvas mode state */
static int canvas_win_id = -1;
static uint32_t *canvas_buf = 0;
static int canvas_pw = 0, canvas_ph = 0;

/* Canvas cursor save/restore state */
static int canvas_cur_px = -1, canvas_cur_py = -1;
static uint32_t canvas_cur_save[8 * 2]; /* FONT_W * 2 rows */

static const uint32_t vga_to_rgb[16] = {
	0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
	0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
	0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
	0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};

static void terminal_update_cursor(void) {
	if (gfx_mode) {
		if (canvas_buf) {
			/* Restore pixels under previous cursor */
			if (canvas_cur_px >= 0 && canvas_cur_py >= 0) {
				for (int r = 0; r < 2; r++) {
					int yy = canvas_cur_py + 14 + r;
					if (yy < 0 || yy >= canvas_ph) continue;
					for (int c = 0; c < FONT_W; c++) {
						int xx = canvas_cur_px + c;
						if (xx < 0 || xx >= canvas_pw) continue;
						canvas_buf[yy * canvas_pw + xx] = canvas_cur_save[r * FONT_W + c];
					}
				}
			}

			/* Calculate new cursor pixel position */
			int px = (int)terminal_column * FONT_W;
			int py = (int)terminal_row * FONT_H;

			/* Save pixels under new cursor position */
			for (int r = 0; r < 2; r++) {
				int yy = py + 14 + r;
				for (int c = 0; c < FONT_W; c++) {
					int xx = px + c;
					if (yy >= 0 && yy < canvas_ph && xx >= 0 && xx < canvas_pw)
						canvas_cur_save[r * FONT_W + c] = canvas_buf[yy * canvas_pw + xx];
					else
						canvas_cur_save[r * FONT_W + c] = 0;
				}
			}
			canvas_cur_px = px;
			canvas_cur_py = py;

			/* Draw underline cursor */
			for (int r = 14; r < 16; r++) {
				int yy = py + r;
				if (yy < 0 || yy >= canvas_ph) continue;
				for (int c = 0; c < FONT_W; c++) {
					int xx = px + c;
					if (xx < 0 || xx >= canvas_pw) continue;
					canvas_buf[yy * canvas_pw + xx] = 0xFFFFFF;
				}
			}
			return;
		}
		gfx_set_cursor((int)(win_x + terminal_column), (int)(win_y + terminal_row));
		return;
	}
	uint16_t pos = terminal_row * VGA_WIDTH + terminal_column;
	outb(0x3D4, 14);
	outb(0x3D5, (uint8_t)(pos >> 8));
	outb(0x3D4, 15);
	outb(0x3D5, (uint8_t)(pos & 0xFF));
}

void terminal_initialize(void) {
	terminal_row = 0;
	terminal_column = 0;
	terminal_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

	if (gfx_is_active()) {
		gfx_mode = 1;
		VGA_WIDTH = gfx_cols();
		VGA_HEIGHT = gfx_rows();
		win_x = 0;
		win_y = 0;
		win_w = VGA_WIDTH;
		win_h = VGA_HEIGHT;
		win_bg_set = 0;
		gfx_clear(0x000000);
		gfx_flip();
	} else {
		gfx_mode = 0;
		VGA_WIDTH = 80;
		VGA_HEIGHT = 25;
		win_x = 0;
		win_y = 0;
		win_w = VGA_WIDTH;
		win_h = VGA_HEIGHT;
		win_bg_set = 0;
		terminal_buffer = VGA_MEMORY;
		for (size_t y = 0; y < VGA_HEIGHT; y++) {
			for (size_t x = 0; x < VGA_WIDTH; x++) {
				const size_t index = y * VGA_WIDTH + x;
				terminal_buffer[index] = vga_entry(' ', terminal_color);
			}
		}
	}
	terminal_update_cursor();
}

void terminal_setcolor(enum vga_color fg, enum vga_color bg) {
	terminal_color = vga_entry_color(fg, bg);
}

void terminal_resetcolor(void) {
	terminal_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

void terminal_putentryat(unsigned char c, uint8_t color, size_t x, size_t y) {
	if (gfx_mode) {
		uint32_t fg = vga_to_rgb[color & 0x0F];
		uint32_t bg = win_bg_set ? win_bg_color : vga_to_rgb[(color >> 4) & 0x0F];
		if (canvas_buf) {
			/* Draw into canvas at relative position */
			gfx_buf_draw_char(canvas_buf, canvas_pw, canvas_ph,
			                  (int)x * FONT_W, (int)y * FONT_H, (char)c, fg, bg);
			return;
		}
		size_t abs_x = win_x + x;
		size_t abs_y = win_y + y;
		gfx_putchar_at((int)abs_x, (int)abs_y, c, fg, bg);
		gfx_flip_rect((int)abs_x * FONT_W, (int)abs_y * FONT_H, FONT_W, FONT_H);
		return;
	}
	const size_t index = y * VGA_WIDTH + x;
	terminal_buffer[index] = vga_entry(c, color);
}

static void terminal_scroll_up(void) {
	if (gfx_mode) {
		uint32_t bg = win_bg_set ? win_bg_color : vga_to_rgb[(terminal_color >> 4) & 0x0F];

		if (canvas_buf) {
			/* Scroll within canvas buffer */
			uint32_t pw = (uint32_t)win_w * FONT_W;
			uint32_t ph = (uint32_t)win_h * FONT_H;
			if (pw > (uint32_t)canvas_pw) pw = (uint32_t)canvas_pw;
			if (ph > (uint32_t)canvas_ph) ph = (uint32_t)canvas_ph;

			for (uint32_t row = 0; row < ph - FONT_H; row++) {
				memcpy(canvas_buf + row * canvas_pw,
				       canvas_buf + (row + FONT_H) * canvas_pw,
				       pw * 4);
			}
			uint32_t start_y = ph - FONT_H;
			for (uint32_t y = start_y; y < ph; y++) {
				uint32_t *rowp = canvas_buf + y * canvas_pw;
				for (uint32_t x = 0; x < pw; x++)
					rowp[x] = bg;
			}
			return;
		}

		uint32_t* bb = gfx_backbuffer();
		uint32_t pitch4 = gfx_pitch() / 4;

		/* Pixel coords of the window region */
		uint32_t px = (uint32_t)win_x * FONT_W;
		uint32_t py = (uint32_t)win_y * FONT_H;
		uint32_t pw = (uint32_t)win_w * FONT_W;
		uint32_t ph = (uint32_t)win_h * FONT_H;

		/* Shift up by FONT_H pixel rows within the window */
		for (uint32_t row = py; row < py + ph - FONT_H; row++) {
			memcpy(bb + row * pitch4 + px,
			       bb + (row + FONT_H) * pitch4 + px,
			       pw * 4);
		}
		/* Clear bottom character row */
		uint32_t start_y = py + ph - FONT_H;
		for (uint32_t y = start_y; y < py + ph; y++) {
			uint32_t* rowp = bb + y * pitch4 + px;
			for (uint32_t x = 0; x < pw; x++)
				rowp[x] = bg;
		}
		gfx_flip();
		return;
	}
	memmove(terminal_buffer,
		terminal_buffer + VGA_WIDTH,
		VGA_WIDTH * (VGA_HEIGHT - 1) * sizeof(uint16_t));
	for (size_t x = 0; x < VGA_WIDTH; x++) {
		terminal_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', terminal_color);
	}
}

void terminal_putchar(char c) {
	unsigned char uc = c;

	if (canvas_buf) wm_mark_dirty();

	if (c == '\b') {
		if (terminal_column > 0) {
			terminal_column--;
		} else if (terminal_row > 0) {
			terminal_row--;
			terminal_column = win_w - 1;
		}
		terminal_putentryat(' ', terminal_color, terminal_column, terminal_row);
		terminal_update_cursor();
		return;
	}

	if (c == '\n') {
		terminal_column = 0;
		if (++terminal_row == win_h) {
			terminal_scroll_up();
			terminal_row = win_h - 1;
		}
		terminal_update_cursor();
		return;
	}

	terminal_putentryat(uc, terminal_color, terminal_column, terminal_row);
	if (++terminal_column == win_w) {
		terminal_column = 0;
		if (++terminal_row == win_h) {
			terminal_scroll_up();
			terminal_row = win_h - 1;
		}
	}
	terminal_update_cursor();
}

void terminal_write(const char* data, size_t size) {
	for (size_t i = 0; i < size; i++)
		terminal_putchar(data[i]);
}

void terminal_writestring(const char* data) {
	terminal_write(data, strlen(data));
}

void terminal_clear(void) {
	if (gfx_mode) {
		uint32_t bg = win_bg_set ? win_bg_color : vga_to_rgb[(terminal_color >> 4) & 0x0F];
		if (canvas_buf) {
			/* Clear canvas buffer */
			int total = canvas_pw * canvas_ph;
			for (int i = 0; i < total; i++)
				canvas_buf[i] = bg;
		} else {
			/* Only clear the window region */
			gfx_fill_rect((int)(win_x * FONT_W), (int)(win_y * FONT_H),
			              (int)(win_w * FONT_W), (int)(win_h * FONT_H), bg);
			gfx_flip();
		}
	} else {
		for (size_t y = 0; y < VGA_HEIGHT; y++) {
			for (size_t x = 0; x < VGA_WIDTH; x++) {
				terminal_buffer[y * VGA_WIDTH + x] = vga_entry(' ', terminal_color);
			}
		}
	}
	terminal_row = 0;
	terminal_column = 0;
	terminal_update_cursor();
}

size_t terminal_get_column(void) {
	return terminal_column;
}

size_t terminal_get_row(void) {
	return terminal_row;
}

size_t terminal_get_width(void) {
	return win_w;
}

void terminal_set_cursor(size_t col, size_t row) {
	if (col >= win_w) col = win_w - 1;
	if (row >= win_h) row = win_h - 1;
	terminal_column = col;
	terminal_row = row;
	terminal_update_cursor();
}

void terminal_set_window(size_t x, size_t y, size_t w, size_t h) {
	win_x = x;
	win_y = y;
	win_w = w;
	win_h = h;
	terminal_row = 0;
	terminal_column = 0;
}

void terminal_set_window_bg(uint32_t color) {
	win_bg_color = color;
	win_bg_set = 1;
}

size_t terminal_get_win_x(void) { return win_x; }
size_t terminal_get_win_y(void) { return win_y; }
size_t terminal_get_win_w(void) { return win_w; }
size_t terminal_get_win_h(void) { return win_h; }

void terminal_set_canvas(int win_id, uint32_t *canvas, int pw, int ph) {
	canvas_win_id = win_id;
	canvas_buf = canvas;
	canvas_pw = pw;
	canvas_ph = ph;
	/* Compute character grid from pixel dimensions */
	win_w = (size_t)(pw / FONT_W);
	win_h = (size_t)(ph / FONT_H);
	win_x = 0;
	win_y = 0;
	terminal_row = 0;
	terminal_column = 0;
	canvas_cur_px = -1;
	canvas_cur_py = -1;
}

void terminal_clear_canvas(void) {
	canvas_win_id = -1;
	canvas_buf = 0;
	canvas_pw = 0;
	canvas_ph = 0;
	canvas_cur_px = -1;
	canvas_cur_py = -1;
}

void terminal_notify_canvas_resize(int win_id, uint32_t *canvas, int pw, int ph) {
	if (canvas_win_id != win_id) return;
	canvas_buf = canvas;
	canvas_pw = pw;
	canvas_ph = ph;
	win_w = (size_t)(pw / FONT_W);
	win_h = (size_t)(ph / FONT_H);
	if (terminal_column >= win_w) terminal_column = win_w > 0 ? win_w - 1 : 0;
	if (terminal_row >= win_h) terminal_row = win_h > 0 ? win_h - 1 : 0;
	canvas_cur_px = -1;
	canvas_cur_py = -1;
}
