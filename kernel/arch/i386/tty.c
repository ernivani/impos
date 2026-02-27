#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/tty.h>
#include <kernel/gfx.h>
#include <kernel/io.h>
#include <kernel/wm.h>

#include "vga.h"

/* Forward declarations for ANSI parser */
static void terminal_scroll_up(void);
void terminal_putentryat(unsigned char c, uint8_t color, size_t x, size_t y);
void terminal_clear(void);

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

/* ---- ANSI escape sequence parser ---- */

#define ANSI_NORMAL  0
#define ANSI_ESC     1  /* received ESC (0x1B) */
#define ANSI_CSI     2  /* received ESC [ */

#define ANSI_MAX_PARAMS 8

static int    ansi_state = ANSI_NORMAL;
static int    ansi_params[ANSI_MAX_PARAMS];
static int    ansi_param_count = 0;
static int    ansi_bold = 0;
static uint8_t ansi_fg = VGA_COLOR_LIGHT_GREY;
static uint8_t ansi_bg = VGA_COLOR_BLACK;

/* ANSI color index (0-7) → VGA color index (0-7) */
static const uint8_t ansi_to_vga[8] = {
	0, /* 0 black   → VGA 0 */
	4, /* 1 red     → VGA 4 */
	2, /* 2 green   → VGA 2 */
	6, /* 3 yellow  → VGA 6 (brown) */
	1, /* 4 blue    → VGA 1 */
	5, /* 5 magenta → VGA 5 */
	3, /* 6 cyan    → VGA 3 */
	7, /* 7 white   → VGA 7 (light grey) */
};

static void ansi_apply_sgr(void) {
	if (ansi_param_count == 0) {
		/* ESC[m with no params = reset */
		ansi_params[0] = 0;
		ansi_param_count = 1;
	}
	for (int i = 0; i < ansi_param_count; i++) {
		int p = ansi_params[i];
		if (p == 0) {
			ansi_bold = 0;
			ansi_fg = VGA_COLOR_LIGHT_GREY;
			ansi_bg = VGA_COLOR_BLACK;
		} else if (p == 1) {
			ansi_bold = 1;
			if (ansi_fg < 8) ansi_fg |= 8;
		} else if (p == 2 || p == 22) {
			ansi_bold = 0;
			if (ansi_fg >= 8) ansi_fg &= 7;
		} else if (p == 7) {
			/* Reverse video */
			uint8_t tmp = ansi_fg;
			ansi_fg = ansi_bg;
			ansi_bg = tmp;
		} else if (p >= 30 && p <= 37) {
			ansi_fg = ansi_to_vga[p - 30];
			if (ansi_bold) ansi_fg |= 8;
		} else if (p == 39) {
			ansi_fg = ansi_bold ? VGA_COLOR_WHITE : VGA_COLOR_LIGHT_GREY;
		} else if (p >= 40 && p <= 47) {
			ansi_bg = ansi_to_vga[p - 40];
		} else if (p == 49) {
			ansi_bg = VGA_COLOR_BLACK;
		} else if (p >= 90 && p <= 97) {
			ansi_fg = ansi_to_vga[p - 90] | 8;
		} else if (p >= 100 && p <= 107) {
			ansi_bg = ansi_to_vga[p - 100] | 8;
		}
		/* 38;5;N and 48;5;N (256-color) — skip N, handled by ignoring unknowns */
	}
	terminal_color = vga_entry_color(ansi_fg, ansi_bg);
}

static const uint32_t vga_to_rgb[16] = {
	0xFF000000, 0xFF0000AA, 0xFF00AA00, 0xFF00AAAA,
	0xFFAA0000, 0xFFAA00AA, 0xFFAA5500, 0xFFAAAAAA,
	0xFF555555, 0xFF5555FF, 0xFF55FF55, 0xFF55FFFF,
	0xFFFF5555, 0xFFFF55FF, 0xFFFFFF55, 0xFFFFFFFF
};

static void ansi_execute_csi(char cmd) {
	int n = (ansi_param_count > 0) ? ansi_params[0] : 0;
	int m_val = (ansi_param_count > 1) ? ansi_params[1] : 0;

	switch (cmd) {
	case 'm': /* SGR — Select Graphic Rendition */
		ansi_apply_sgr();
		break;
	case 'J': /* Erase in Display */
		if (n == 2 || n == 3) {
			terminal_clear();
		} else if (n == 0) {
			/* Clear from cursor to end */
			for (size_t x = terminal_column; x < win_w; x++)
				terminal_putentryat(' ', terminal_color, x, terminal_row);
			for (size_t y = terminal_row + 1; y < win_h; y++)
				for (size_t x = 0; x < win_w; x++)
					terminal_putentryat(' ', terminal_color, x, y);
		}
		break;
	case 'K': /* Erase in Line */
		if (n == 0) {
			for (size_t x = terminal_column; x < win_w; x++)
				terminal_putentryat(' ', terminal_color, x, terminal_row);
		} else if (n == 1) {
			for (size_t x = 0; x <= terminal_column; x++)
				terminal_putentryat(' ', terminal_color, x, terminal_row);
		} else if (n == 2) {
			for (size_t x = 0; x < win_w; x++)
				terminal_putentryat(' ', terminal_color, x, terminal_row);
		}
		break;
	case 'H': case 'f': /* Cursor Position (1-based) */
		if (n == 0) n = 1;
		if (m_val == 0) m_val = 1;
		terminal_row = (size_t)(n - 1);
		terminal_column = (size_t)(m_val - 1);
		if (terminal_row >= win_h) terminal_row = win_h - 1;
		if (terminal_column >= win_w) terminal_column = win_w - 1;
		break;
	case 'A': /* Cursor Up */
		if (n == 0) n = 1;
		if ((size_t)n > terminal_row) terminal_row = 0;
		else terminal_row -= (size_t)n;
		break;
	case 'B': /* Cursor Down */
		if (n == 0) n = 1;
		terminal_row += (size_t)n;
		if (terminal_row >= win_h) terminal_row = win_h - 1;
		break;
	case 'C': /* Cursor Forward */
		if (n == 0) n = 1;
		terminal_column += (size_t)n;
		if (terminal_column >= win_w) terminal_column = win_w - 1;
		break;
	case 'D': /* Cursor Back */
		if (n == 0) n = 1;
		if ((size_t)n > terminal_column) terminal_column = 0;
		else terminal_column -= (size_t)n;
		break;
	case 'G': /* Cursor Horizontal Absolute (1-based) */
		if (n == 0) n = 1;
		terminal_column = (size_t)(n - 1);
		if (terminal_column >= win_w) terminal_column = win_w - 1;
		break;
	default:
		/* Silently ignore unrecognized CSI commands (l, h, etc.) */
		break;
	}
}

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
	ansi_state = ANSI_NORMAL;
	ansi_bold = 0;
	ansi_fg = VGA_COLOR_LIGHT_GREY;
	ansi_bg = VGA_COLOR_BLACK;

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
	ansi_fg = (uint8_t)fg;
	ansi_bg = (uint8_t)bg;
}

void terminal_resetcolor(void) {
	terminal_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
	ansi_bold = 0;
	ansi_fg = VGA_COLOR_LIGHT_GREY;
	ansi_bg = VGA_COLOR_BLACK;
	ansi_state = ANSI_NORMAL;
}

void terminal_putentryat(unsigned char c, uint8_t color, size_t x, size_t y) {
	if (gfx_mode) {
		uint32_t fg = vga_to_rgb[color & 0x0F];
		uint32_t bg = win_bg_set ? win_bg_color : vga_to_rgb[(color >> 4) & 0x0F];
		if (canvas_buf) {
			/* Draw into canvas: clear cell background, then SDF smooth glyph */
			gfx_surface_t cs = { canvas_buf, canvas_pw, canvas_ph, canvas_pw };
			int px = (int)x * FONT_W;
			int py = (int)y * FONT_H;
			gfx_surf_fill_rect(&cs, px, py, FONT_W, FONT_H, bg);
			gfx_surf_draw_char_smooth(&cs, px, py, (char)c, fg, 1);
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
	unsigned char uc = (unsigned char)c;

	if (canvas_buf) wm_mark_dirty();

	/* ---- ANSI escape sequence state machine ---- */
	if (ansi_state == ANSI_ESC) {
		if (c == '[') {
			ansi_state = ANSI_CSI;
			ansi_param_count = 0;
			memset(ansi_params, 0, sizeof(ansi_params));
			return;
		}
		/* Not a CSI — discard ESC, fall through to print char */
		ansi_state = ANSI_NORMAL;
	} else if (ansi_state == ANSI_CSI) {
		if (c >= '0' && c <= '9') {
			if (ansi_param_count == 0) ansi_param_count = 1;
			ansi_params[ansi_param_count - 1] =
				ansi_params[ansi_param_count - 1] * 10 + (c - '0');
			return;
		} else if (c == ';') {
			if (ansi_param_count < ANSI_MAX_PARAMS)
				ansi_param_count++;
			return;
		} else if (c == '?') {
			/* Private mode prefix (ESC[?25l etc.) — consume */
			return;
		} else if (c >= 0x40 && c <= 0x7E) {
			/* Final byte — execute CSI command */
			ansi_execute_csi(c);
			ansi_state = ANSI_NORMAL;
			terminal_update_cursor();
			return;
		} else {
			/* Unexpected — abort sequence */
			ansi_state = ANSI_NORMAL;
			return;
		}
	}

	if (uc == 0x1B) { /* ESC */
		ansi_state = ANSI_ESC;
		return;
	}

	/* ---- Normal character handling ---- */

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

	if (c == '\r') {
		terminal_column = 0;
		terminal_update_cursor();
		return;
	}

	if (c == '\t') {
		size_t next = (terminal_column + 8) & ~(size_t)7;
		if (next >= win_w) next = win_w - 1;
		terminal_column = next;
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
