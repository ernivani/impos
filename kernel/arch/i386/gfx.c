#include <kernel/gfx.h>
#include <kernel/multiboot.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>

#include "font8x16.h"

static uint32_t* framebuffer;
static uint32_t* backbuf;
static uint32_t fb_width;
static uint32_t fb_height;
static uint32_t fb_pitch;   /* bytes per scanline */
static uint32_t fb_bpp;
static int gfx_active;
static int have_backbuffer;

/* Cursor state */
static int cursor_col = -1;
static int cursor_row = -1;
static int prev_cursor_col = -1;
static int prev_cursor_row = -1;

int gfx_init(multiboot_info_t* mbi) {
    gfx_active = 0;
    if (!mbi)
        return 0;

    uint32_t addr = 0;

    /* Try GRUB2 framebuffer extension (bit 12) first */
    if (mbi->flags & (1 << 12)) {
        addr      = (uint32_t)mbi->framebuffer_addr;
        fb_width  = mbi->framebuffer_width;
        fb_height = mbi->framebuffer_height;
        fb_pitch  = mbi->framebuffer_pitch;
        fb_bpp    = mbi->framebuffer_bpp;
        /* type 0 = indexed, type 1 = direct RGB, type 2 = EGA text */
        if (mbi->framebuffer_type == 2) {
            /* EGA text mode — fall back */
            return 0;
        }
    }
    /* Else try VBE (bit 11) */
    else if (mbi->flags & (1 << 11)) {
        vbe_mode_info_t* vbe = (vbe_mode_info_t*)(uintptr_t)mbi->vbe_mode_info;
        if (!vbe)
            return 0;
        addr      = vbe->physbase;
        fb_width  = vbe->width;
        fb_height = vbe->height;
        fb_pitch  = vbe->pitch;
        fb_bpp    = vbe->bpp;
    } else {
        return 0;
    }

    /* Validate */
    if (fb_bpp != 32 || fb_width == 0 || fb_height == 0 || addr == 0)
        return 0;

    framebuffer = (uint32_t*)(uintptr_t)addr;

    /* Allocate back buffer */
    size_t fb_size = fb_height * fb_pitch;
    backbuf = (uint32_t*)malloc(fb_size);
    if (backbuf) {
        have_backbuffer = 1;
    } else {
        /* No double buffering — draw directly */
        backbuf = framebuffer;
        have_backbuffer = 0;
    }

    /* Clear to black */
    memset(backbuf, 0, fb_size);
    if (have_backbuffer)
        memcpy(framebuffer, backbuf, fb_size);

    gfx_active = 1;
    return 1;
}

int gfx_is_active(void) {
    return gfx_active;
}

uint32_t gfx_width(void)  { return fb_width; }
uint32_t gfx_height(void) { return fb_height; }
uint32_t gfx_pitch(void)  { return fb_pitch; }
uint32_t gfx_bpp(void)    { return fb_bpp; }

uint32_t gfx_cols(void)   { return fb_width / FONT_W; }
uint32_t gfx_rows(void)   { return fb_height / FONT_H; }

uint32_t* gfx_backbuffer(void) { return backbuf; }

/* --- Pixel primitives --- */

void gfx_put_pixel(int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || (uint32_t)x >= fb_width || (uint32_t)y >= fb_height)
        return;
    backbuf[y * (fb_pitch / 4) + x] = color;
}

void gfx_fill_rect(int x, int y, int w, int h, uint32_t color) {
    /* Clip */
    int x0 = x, y0 = y;
    int x1 = x + w, y1 = y + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)fb_width) x1 = (int)fb_width;
    if (y1 > (int)fb_height) y1 = (int)fb_height;
    if (x0 >= x1 || y0 >= y1) return;

    uint32_t pitch4 = fb_pitch / 4;
    for (int row = y0; row < y1; row++) {
        uint32_t* dst = backbuf + row * pitch4 + x0;
        for (int col = 0; col < x1 - x0; col++)
            dst[col] = color;
    }
}

void gfx_draw_rect(int x, int y, int w, int h, uint32_t color) {
    gfx_fill_rect(x, y, w, 1, color);          /* top */
    gfx_fill_rect(x, y + h - 1, w, 1, color);  /* bottom */
    gfx_fill_rect(x, y, 1, h, color);          /* left */
    gfx_fill_rect(x + w - 1, y, 1, h, color);  /* right */
}

void gfx_draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = x1 - x0;
    int dy = y1 - y0;
    int sx = dx > 0 ? 1 : -1;
    int sy = dy > 0 ? 1 : -1;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;

    int err = dx - dy;
    while (1) {
        gfx_put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

void gfx_clear(uint32_t color) {
    uint32_t pitch4 = fb_pitch / 4;
    for (uint32_t y = 0; y < fb_height; y++) {
        uint32_t* row = backbuf + y * pitch4;
        for (uint32_t x = 0; x < fb_width; x++)
            row[x] = color;
    }
}

/* --- Text rendering --- */

void gfx_draw_char(int px, int py, char c, uint32_t fg, uint32_t bg) {
    unsigned char uc = (unsigned char)c;
    const uint8_t* glyph = font8x16[uc];
    uint32_t pitch4 = fb_pitch / 4;

    for (int row = 0; row < FONT_H; row++) {
        int yy = py + row;
        if (yy < 0 || (uint32_t)yy >= fb_height) continue;
        uint8_t bits = glyph[row];
        uint32_t* dst = backbuf + yy * pitch4 + px;
        for (int col = 0; col < FONT_W; col++) {
            int xx = px + col;
            if (xx < 0 || (uint32_t)xx >= fb_width) continue;
            dst[col] = (bits & (0x80 >> col)) ? fg : bg;
        }
    }
}

void gfx_draw_string(int px, int py, const char* s, uint32_t fg, uint32_t bg) {
    while (*s) {
        gfx_draw_char(px, py, *s, fg, bg);
        px += FONT_W;
        s++;
    }
}

void gfx_draw_char_nobg(int px, int py, char c, uint32_t fg) {
    unsigned char uc = (unsigned char)c;
    const uint8_t* glyph = font8x16[uc];
    uint32_t pitch4 = fb_pitch / 4;

    for (int row = 0; row < FONT_H; row++) {
        int yy = py + row;
        if (yy < 0 || (uint32_t)yy >= fb_height) continue;
        uint8_t bits = glyph[row];
        for (int col = 0; col < FONT_W; col++) {
            if (bits & (0x80 >> col)) {
                int xx = px + col;
                if (xx >= 0 && (uint32_t)xx < fb_width)
                    backbuf[yy * pitch4 + xx] = fg;
            }
        }
    }
}

void gfx_draw_string_nobg(int px, int py, const char* s, uint32_t fg) {
    while (*s) {
        gfx_draw_char_nobg(px, py, *s, fg);
        px += FONT_W;
        s++;
    }
}

void gfx_putchar_at(int col, int row, unsigned char c, uint32_t fg, uint32_t bg) {
    gfx_draw_char(col * FONT_W, row * FONT_H, (char)c, fg, bg);
}

/* --- Cursor --- */

void gfx_set_cursor(int col, int row) {
    uint32_t pitch4 = fb_pitch / 4;

    /* Erase previous cursor by restoring clean backbuffer data to framebuffer */
    if (prev_cursor_col >= 0 && prev_cursor_row >= 0 &&
        (prev_cursor_col != col || prev_cursor_row != row)) {
        gfx_flip_rect(prev_cursor_col * FONT_W, prev_cursor_row * FONT_H,
                       FONT_W, FONT_H);
    }

    /* First flush the new cell's clean backbuffer content to framebuffer */
    int px = col * FONT_W;
    int py = row * FONT_H;
    gfx_flip_rect(px, py, FONT_W, FONT_H);

    /* Draw underline cursor at rows 14-15 directly to framebuffer (not backbuffer) */
    for (int r = 14; r < 16; r++) {
        int yy = py + r;
        if (yy < 0 || (uint32_t)yy >= fb_height) continue;
        for (int c = 0; c < FONT_W; c++) {
            int xx = px + c;
            if (xx < 0 || (uint32_t)xx >= fb_width) continue;
            framebuffer[yy * pitch4 + xx] = GFX_WHITE;
        }
    }

    prev_cursor_col = col;
    prev_cursor_row = row;
    cursor_col = col;
    cursor_row = row;
}

/* --- Double buffering --- */

void gfx_flip(void) {
    if (!have_backbuffer) return;
    memcpy(framebuffer, backbuf, fb_height * fb_pitch);
}

void gfx_flip_rect(int x, int y, int w, int h) {
    if (!have_backbuffer) return;

    /* Clip */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)fb_width) w = (int)fb_width - x;
    if (y + h > (int)fb_height) h = (int)fb_height - y;
    if (w <= 0 || h <= 0) return;

    uint32_t pitch4 = fb_pitch / 4;
    for (int row = y; row < y + h; row++) {
        memcpy(&framebuffer[row * pitch4 + x],
               &backbuf[row * pitch4 + x],
               (size_t)w * 4);
    }
}
