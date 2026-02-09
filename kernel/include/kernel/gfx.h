#ifndef _KERNEL_GFX_H
#define _KERNEL_GFX_H

#include <stdint.h>
#include <kernel/multiboot.h>

#define FONT_W  8
#define FONT_H  16

int  gfx_init(multiboot_info_t* mbi);
int  gfx_is_active(void);

uint32_t gfx_width(void);
uint32_t gfx_height(void);
uint32_t gfx_pitch(void);
uint32_t gfx_bpp(void);

uint32_t gfx_cols(void);
uint32_t gfx_rows(void);

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

uint32_t* gfx_backbuffer(void);
uint32_t* gfx_framebuffer(void);

void gfx_blend_pixel(int x, int y, uint32_t color);
void gfx_fill_rect_alpha(int x, int y, int w, int h, uint32_t color);
void gfx_draw_char_alpha(int x, int y, char c, uint32_t fg_with_alpha);

/* Mouse cursor rendering (draws directly to framebuffer) */
void gfx_draw_mouse_cursor(int x, int y);
void gfx_restore_mouse_cursor(void);

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
