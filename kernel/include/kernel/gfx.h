#ifndef _KERNEL_GFX_H
#define _KERNEL_GFX_H

#include <stdint.h>
#include <kernel/multiboot.h>

#define FONT_W  8
#define FONT_H  16

/* ═══ Surface abstraction ═══════════════════════════════════════ */

typedef struct {
    uint32_t *buf;
    int w, h, pitch;  /* pitch in pixels */
} gfx_surface_t;

gfx_surface_t gfx_get_surface(void);

/* ═══ Init / query ══════════════════════════════════════════════ */

int  gfx_init(multiboot_info_t* mbi);
void gfx_init_font_sdf(void);
int  gfx_is_active(void);

uint32_t gfx_width(void);
uint32_t gfx_height(void);
uint32_t gfx_pitch(void);
uint32_t gfx_bpp(void);

uint32_t gfx_cols(void);
uint32_t gfx_rows(void);

/* ═══ Backbuffer drawing (convenience wrappers) ═════════════════ */

void gfx_put_pixel(int x, int y, uint32_t color);
void gfx_fill_rect(int x, int y, int w, int h, uint32_t color);
void gfx_draw_rect(int x, int y, int w, int h, uint32_t color);
void gfx_draw_line(int x0, int y0, int x1, int y1, uint32_t color);
void gfx_clear(uint32_t color);

void gfx_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg);
void gfx_draw_string(int x, int y, const char* s, uint32_t fg, uint32_t bg);
void gfx_draw_char_nobg(int x, int y, char c, uint32_t fg);
void gfx_draw_string_nobg(int x, int y, const char* s, uint32_t fg);

void gfx_putchar_at(int col, int row, unsigned char c, uint32_t fg, uint32_t bg);
void gfx_set_cursor(int col, int row);

void gfx_flip(void);
void gfx_flip_rect(int x, int y, int w, int h);
void gfx_overlay_darken(int x, int y, int w, int h, uint8_t alpha);
void gfx_crossfade(int steps, int delay_ms);

uint32_t* gfx_backbuffer(void);
uint32_t* gfx_framebuffer(void);

void gfx_blend_pixel(int x, int y, uint32_t color);
void gfx_fill_rect_alpha(int x, int y, int w, int h, uint32_t color);
void gfx_draw_char_alpha(int x, int y, char c, uint32_t fg_with_alpha);

/* ═══ Buffer-targeted drawing (legacy wrappers) ═════════════════ */

void gfx_buf_put_pixel(uint32_t *buf, int bw, int bh, int x, int y, uint32_t color);
void gfx_buf_fill_rect(uint32_t *buf, int bw, int bh, int x, int y, int w, int h, uint32_t color);
void gfx_buf_draw_rect(uint32_t *buf, int bw, int bh, int x, int y, int w, int h, uint32_t color);
void gfx_buf_draw_line(uint32_t *buf, int bw, int bh, int x0, int y0, int x1, int y1, uint32_t color);
void gfx_buf_draw_char(uint32_t *buf, int bw, int bh, int px, int py, char c, uint32_t fg, uint32_t bg);
void gfx_buf_draw_string(uint32_t *buf, int bw, int bh, int px, int py, const char *s, uint32_t fg, uint32_t bg);
void gfx_blit_buffer(int dst_x, int dst_y, uint32_t *src, int sw, int sh);

/* ═══ Surface-targeted drawing ══════════════════════════════════ */

void gfx_surf_put_pixel(gfx_surface_t *s, int x, int y, uint32_t color);
void gfx_surf_fill_rect(gfx_surface_t *s, int x, int y, int w, int h, uint32_t color);
void gfx_surf_draw_rect(gfx_surface_t *s, int x, int y, int w, int h, uint32_t color);
void gfx_surf_draw_line(gfx_surface_t *s, int x0, int y0, int x1, int y1, uint32_t color);
void gfx_surf_draw_char(gfx_surface_t *s, int x, int y, char c, uint32_t fg, uint32_t bg);
void gfx_surf_draw_string(gfx_surface_t *s, int x, int y, const char *str, uint32_t fg, uint32_t bg);
void gfx_surf_blend_pixel(gfx_surface_t *s, int x, int y, uint32_t color, uint8_t alpha);
void gfx_surf_fill_rect_alpha(gfx_surface_t *s, int x, int y, int w, int h, uint32_t color, uint8_t alpha);

/* ═══ Geometry helpers (surface-targeted) ═══════════════════════ */

void gfx_surf_fill_circle(gfx_surface_t *s, int cx, int cy, int r, uint32_t color);
void gfx_surf_fill_circle_aa(gfx_surface_t *s, int cx, int cy, int r, uint32_t color);
void gfx_surf_circle_ring(gfx_surface_t *s, int cx, int cy, int r, int thick, uint32_t color);
void gfx_surf_rounded_rect(gfx_surface_t *s, int x, int y, int w, int h, int r, uint32_t color);
void gfx_surf_rounded_rect_alpha(gfx_surface_t *s, int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha);
void gfx_surf_rounded_rect_outline(gfx_surface_t *s, int x, int y, int w, int h, int r, uint32_t color);
void gfx_surf_draw_char_scaled(gfx_surface_t *s, int x, int y, char c, uint32_t fg, int scale);
void gfx_surf_draw_string_scaled(gfx_surface_t *s, int x, int y, const char *str, uint32_t fg, int scale);
void gfx_surf_draw_char_smooth(gfx_surface_t *s, int x, int y, char c, uint32_t fg, int scale);
void gfx_surf_draw_string_smooth(gfx_surface_t *s, int x, int y, const char *str, uint32_t fg, int scale);

/* ═══ Geometry helpers (backbuffer convenience) ═════════════════ */

void gfx_fill_circle(int cx, int cy, int r, uint32_t color);
void gfx_fill_circle_aa(int cx, int cy, int r, uint32_t color);
void gfx_circle_ring(int cx, int cy, int r, int thick, uint32_t color);
void gfx_rounded_rect(int x, int y, int w, int h, int r, uint32_t color);
void gfx_rounded_rect_alpha(int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha);
void gfx_rounded_rect_outline(int x, int y, int w, int h, int r, uint32_t color);
void gfx_draw_char_scaled(int x, int y, char c, uint32_t fg, int scale);
void gfx_draw_string_scaled(int x, int y, const char *str, uint32_t fg, int scale);
void gfx_draw_char_smooth(int x, int y, char c, uint32_t fg, int scale);
void gfx_draw_string_smooth(int x, int y, const char *str, uint32_t fg, int scale);
int  gfx_string_scaled_w(const char *str, int scale);

/* ═══ Mouse cursor rendering ═══════════════════════════════════ */

#define GFX_CURSOR_ARROW  0
#define GFX_CURSOR_HAND   1
#define GFX_CURSOR_TEXT   2

void gfx_set_cursor_type(int type);
int  gfx_get_cursor_type(void);
void gfx_draw_mouse_cursor(int x, int y);
void gfx_restore_mouse_cursor(void);

/* ═══ RAM detection ════════════════════════════════════════════ */

uint32_t gfx_get_system_ram_mb(void);

/* ═══ Box blur ═════════════════════════════════════════════════ */

void gfx_box_blur(uint32_t *buf, int w, int h, int radius);

/* ═══ Alpha blit ═══════════════════════════════════════════════ */

void gfx_blit_buffer_alpha(int dst_x, int dst_y, uint32_t *src, int sw, int sh, uint8_t alpha);

/* ═══ Large font (16x32) ══════════════════════════════════════ */

#define FONT_LARGE_W 16
#define FONT_LARGE_H 32

void gfx_draw_char_large(int x, int y, char c, uint32_t fg, uint32_t bg);
void gfx_draw_string_large(int x, int y, const char *s, uint32_t fg, uint32_t bg);
void gfx_surf_draw_char_large(gfx_surface_t *s, int x, int y, char c, uint32_t fg, uint32_t bg);
void gfx_surf_draw_string_large(gfx_surface_t *s, int x, int y, const char *str, uint32_t fg, uint32_t bg);
void gfx_init_font_large(void);

/* ═══ Dirty rect flip ═════════════════════════════════════════ */

typedef struct {
    int x, y, w, h;
} gfx_rect_t;

void gfx_flip_rects(gfx_rect_t *rects, int count);

/* ═══ Color macros ═════════════════════════════════════════════ */

#define GFX_RGB(r,g,b) (((uint32_t)(r)<<16)|((uint32_t)(g)<<8)|(uint32_t)(b))
#define GFX_RGBA(r,g,b,a) (((uint32_t)(a)<<24)|((uint32_t)(r)<<16)|((uint32_t)(g)<<8)|(uint32_t)(b))
#define GFX_ALPHA(c) (((c)>>24)&0xFF)
#define GFX_BLACK   0x000000
#define GFX_WHITE   0xFFFFFF
#define GFX_RED     0xFF0000
#define GFX_GREEN   0x00FF00
#define GFX_BLUE    0x0000FF
#define GFX_CYAN    0x00FFFF
#define GFX_YELLOW  0xFFFF00
#define GFX_MAGENTA 0xFF00FF

#endif
