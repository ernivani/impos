#include <kernel/gfx.h>
#include <kernel/multiboot.h>
#include <kernel/idt.h>
#include <kernel/mouse.h>
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
static uint32_t system_ram_mb;

/* Cursor state */
static int cursor_col = -1;
static int cursor_row = -1;
static int prev_cursor_col = -1;
static int prev_cursor_row = -1;

int gfx_init(multiboot_info_t* mbi) {
    gfx_active = 0;
    if (!mbi)
        return 0;

    /* Detect system RAM from multiboot info (flags bit 0) */
    if (mbi->flags & 1)
        system_ram_mb = (mbi->mem_upper + 1024) / 1024;
    else
        system_ram_mb = 16; /* fallback */

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
    gfx_init_font_sdf();
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
uint32_t gfx_get_system_ram_mb(void) { return system_ram_mb; }

uint32_t* gfx_backbuffer(void) { return backbuf; }
uint32_t* gfx_framebuffer(void) { return framebuffer; }

gfx_surface_t gfx_get_surface(void) {
    gfx_surface_t s;
    s.buf = backbuf;
    s.w = (int)fb_width;
    s.h = (int)fb_height;
    s.pitch = (int)(fb_pitch / 4);
    return s;
}

/* ═══ Surface-based core primitives ════════════════════════════ */

void gfx_surf_put_pixel(gfx_surface_t *s, int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || x >= s->w || y >= s->h) return;
    s->buf[y * s->pitch + x] = color;
}

void gfx_surf_fill_rect(gfx_surface_t *s, int x, int y, int w, int h, uint32_t color) {
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;
    if (x0 >= x1 || y0 >= y1) return;
    int width = x1 - x0;
    /* Fill first row */
    uint32_t *first = s->buf + y0 * s->pitch + x0;
    for (int col = 0; col < width; col++)
        first[col] = color;
    /* Replicate to remaining rows via memcpy (uses rep movsd) */
    size_t row_bytes = (size_t)width * 4;
    for (int row = y0 + 1; row < y1; row++)
        memcpy(s->buf + row * s->pitch + x0, first, row_bytes);
}

void gfx_surf_draw_rect(gfx_surface_t *s, int x, int y, int w, int h, uint32_t color) {
    gfx_surf_fill_rect(s, x, y, w, 1, color);
    gfx_surf_fill_rect(s, x, y + h - 1, w, 1, color);
    gfx_surf_fill_rect(s, x, y, 1, h, color);
    gfx_surf_fill_rect(s, x + w - 1, y, 1, h, color);
}

void gfx_surf_draw_line(gfx_surface_t *s, int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = x1 - x0, dy = y1 - y0;
    int sx = dx > 0 ? 1 : -1;
    int sy = dy > 0 ? 1 : -1;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    int err = dx - dy;
    while (1) {
        gfx_surf_put_pixel(s, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

void gfx_surf_draw_char(gfx_surface_t *s, int px, int py, char c, uint32_t fg, uint32_t bg) {
    unsigned char uc = (unsigned char)c;
    const uint8_t *glyph = font8x16[uc];
    for (int row = 0; row < FONT_H; row++) {
        int yy = py + row;
        if (yy < 0 || yy >= s->h) continue;
        uint8_t bits = glyph[row];
        for (int col = 0; col < FONT_W; col++) {
            int xx = px + col;
            if (xx < 0 || xx >= s->w) continue;
            s->buf[yy * s->pitch + xx] = (bits & (0x80 >> col)) ? fg : bg;
        }
    }
}

void gfx_surf_draw_string(gfx_surface_t *s, int px, int py, const char *str, uint32_t fg, uint32_t bg) {
    while (*str) {
        gfx_surf_draw_char(s, px, py, *str, fg, bg);
        px += FONT_W;
        str++;
    }
}

/* ═══ Alpha blending ═══════════════════════════════════════════ */

static inline uint32_t alpha_blend(uint32_t dst, uint32_t src) {
    uint32_t a = (src >> 24) & 0xFF;
    if (a == 255) return src & 0x00FFFFFF;
    if (a == 0) return dst;
    uint32_t inv_a = 255 - a;
    uint32_t sr = (src >> 16) & 0xFF, sg = (src >> 8) & 0xFF, sb = src & 0xFF;
    uint32_t dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
    uint32_t or_ = (sr * a + dr * inv_a) / 255;
    uint32_t og = (sg * a + dg * inv_a) / 255;
    uint32_t ob = (sb * a + db * inv_a) / 255;
    return (or_ << 16) | (og << 8) | ob;
}

static inline uint32_t alpha_blend_sep(uint32_t dst, uint32_t src_rgb, uint8_t alpha) {
    if (alpha == 255) return src_rgb;
    if (alpha == 0) return dst;
    uint32_t inv_a = 255 - alpha;
    uint32_t sr = (src_rgb >> 16) & 0xFF, sg = (src_rgb >> 8) & 0xFF, sb = src_rgb & 0xFF;
    uint32_t dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
    uint32_t or_ = (sr * alpha + dr * inv_a) / 255;
    uint32_t og = (sg * alpha + dg * inv_a) / 255;
    uint32_t ob = (sb * alpha + db * inv_a) / 255;
    return (or_ << 16) | (og << 8) | ob;
}

void gfx_surf_blend_pixel(gfx_surface_t *s, int x, int y, uint32_t color, uint8_t alpha) {
    if (x < 0 || y < 0 || x >= s->w || y >= s->h) return;
    uint32_t idx = y * s->pitch + x;
    s->buf[idx] = alpha_blend_sep(s->buf[idx], color, alpha);
}

void gfx_surf_fill_rect_alpha(gfx_surface_t *s, int x, int y, int w, int h, uint32_t color, uint8_t alpha) {
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;
    if (x0 >= x1 || y0 >= y1) return;
    for (int row = y0; row < y1; row++) {
        uint32_t *dst = s->buf + row * s->pitch + x0;
        for (int col = 0; col < x1 - x0; col++)
            dst[col] = alpha_blend_sep(dst[col], color, alpha);
    }
}

/* ═══ Geometry helpers (surface-based) ═════════════════════════ */

void gfx_surf_fill_circle(gfx_surface_t *s, int cx, int cy, int r, uint32_t color) {
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r)
                gfx_surf_put_pixel(s, cx + dx, cy + dy, color);
}

void gfx_surf_fill_circle_aa(gfx_surface_t *s, int cx, int cy, int r, uint32_t color) {
    int r2 = r * r;
    int inner2 = (r - 1) * (r - 1);
    int range = r2 - inner2;
    if (range < 1) range = 1;
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 > r2) continue;
            if (d2 <= inner2) {
                gfx_surf_put_pixel(s, cx + dx, cy + dy, color);
            } else {
                uint8_t a = (uint8_t)((r2 - d2) * 255 / range);
                gfx_surf_blend_pixel(s, cx + dx, cy + dy, color, a);
            }
        }
}

void gfx_surf_circle_ring(gfx_surface_t *s, int cx, int cy, int r, int thick, uint32_t color) {
    int ro2 = r * r, ri2 = (r - thick) * (r - thick);
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++) {
            int d = dx * dx + dy * dy;
            if (d <= ro2 && d >= ri2)
                gfx_surf_put_pixel(s, cx + dx, cy + dy, color);
        }
}

void gfx_surf_rounded_rect(gfx_surface_t *s, int x, int y, int w, int h, int r, uint32_t color) {
    gfx_surf_fill_rect(s, x + r, y, w - 2 * r, h, color);
    gfx_surf_fill_rect(s, x, y + r, w, h - 2 * r, color);
    for (int cy2 = 0; cy2 < r; cy2++)
        for (int cx2 = 0; cx2 < r; cx2++)
            if ((r - cx2 - 1) * (r - cx2 - 1) + (r - cy2 - 1) * (r - cy2 - 1) <= r * r) {
                gfx_surf_put_pixel(s, x + cx2, y + cy2, color);
                gfx_surf_put_pixel(s, x + w - 1 - cx2, y + cy2, color);
                gfx_surf_put_pixel(s, x + cx2, y + h - 1 - cy2, color);
                gfx_surf_put_pixel(s, x + w - 1 - cx2, y + h - 1 - cy2, color);
            }
}

void gfx_surf_rounded_rect_alpha(gfx_surface_t *s, int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha) {
    /* Central cross with alpha blending */
    for (int row = y + r; row < y + h - r; row++)
        for (int col = x; col < x + w; col++)
            gfx_surf_blend_pixel(s, col, row, color, alpha);
    for (int row = y; row < y + r; row++)
        for (int col = x + r; col < x + w - r; col++)
            gfx_surf_blend_pixel(s, col, row, color, alpha);
    for (int row = y + h - r; row < y + h; row++)
        for (int col = x + r; col < x + w - r; col++)
            gfx_surf_blend_pixel(s, col, row, color, alpha);
    /* Corners */
    for (int cy2 = 0; cy2 < r; cy2++)
        for (int cx2 = 0; cx2 < r; cx2++)
            if ((r - cx2 - 1) * (r - cx2 - 1) + (r - cy2 - 1) * (r - cy2 - 1) <= r * r) {
                gfx_surf_blend_pixel(s, x + cx2, y + cy2, color, alpha);
                gfx_surf_blend_pixel(s, x + w - 1 - cx2, y + cy2, color, alpha);
                gfx_surf_blend_pixel(s, x + cx2, y + h - 1 - cy2, color, alpha);
                gfx_surf_blend_pixel(s, x + w - 1 - cx2, y + h - 1 - cy2, color, alpha);
            }
}

void gfx_surf_rounded_rect_outline(gfx_surface_t *s, int x, int y, int w, int h, int r, uint32_t color) {
    gfx_surf_fill_rect(s, x + r, y, w - 2 * r, 1, color);
    gfx_surf_fill_rect(s, x + r, y + h - 1, w - 2 * r, 1, color);
    gfx_surf_fill_rect(s, x, y + r, 1, h - 2 * r, color);
    gfx_surf_fill_rect(s, x + w - 1, y + r, 1, h - 2 * r, color);
    int R2 = r * r;
    for (int cy2 = 0; cy2 < r; cy2++)
        for (int cx2 = 0; cx2 < r; cx2++) {
            int dx = r - cx2 - 1, dy = r - cy2 - 1;
            int d = dx * dx + dy * dy;
            if (d > R2) continue;
            if ((dx+1)*(dx+1) + dy*dy > R2 ||
                dx*dx + (dy+1)*(dy+1) > R2 ||
                (dx+1)*(dx+1) + (dy+1)*(dy+1) > R2) {
                gfx_surf_put_pixel(s, x + cx2, y + cy2, color);
                gfx_surf_put_pixel(s, x + w - 1 - cx2, y + cy2, color);
                gfx_surf_put_pixel(s, x + cx2, y + h - 1 - cy2, color);
                gfx_surf_put_pixel(s, x + w - 1 - cx2, y + h - 1 - cy2, color);
            }
        }
}

void gfx_surf_draw_char_scaled(gfx_surface_t *s, int px, int py, char c, uint32_t fg, int sc) {
    const uint8_t *g = font8x16[(unsigned char)c];
    for (int row = 0; row < FONT_H; row++) {
        uint8_t bits = g[row];
        for (int col = 0; col < FONT_W; col++)
            if (bits & (0x80 >> col))
                gfx_surf_fill_rect(s, px + col * sc, py + row * sc, sc, sc, fg);
    }
}

void gfx_surf_draw_string_scaled(gfx_surface_t *s, int px, int py, const char *str, uint32_t fg, int sc) {
    while (*str) {
        gfx_surf_draw_char_scaled(s, px, py, *str, fg, sc);
        px += FONT_W * sc;
        str++;
    }
}

/* ═══ Signed Distance Field font rendering ═══════════════════ */

static inline int font_texel(unsigned char c, int x, int y) {
    if (x < 0 || x >= FONT_W || y < 0 || y >= FONT_H) return 0;
    return (font8x16[c][y] >> (7 - x)) & 1;
}

/*
 * SDF table: for each font texel, stores the signed distance to the
 * nearest edge (boundary between inside/outside), in 1/8-texel units.
 * Positive = inside glyph, negative = outside.
 * 256 chars × 16 rows × 8 cols = 32 KB.
 */
static int8_t font_sdf[256][FONT_H][FONT_W];

static int isqrt_int(int n) {
    if (n <= 0) return 0;
    int x = n, y = (x + 1) >> 1;
    while (y < x) { x = y; y = (x + n / x) >> 1; }
    return x;
}

void gfx_init_font_sdf(void) {
    for (int ch = 0; ch < 256; ch++) {
        for (int y = 0; y < FONT_H; y++) {
            for (int x = 0; x < FONT_W; x++) {
                int inside = font_texel((unsigned char)ch, x, y);
                int min_d2 = 9999;

                /* Search ±5 texels for nearest opposite-type texel */
                for (int dy = -5; dy <= 5; dy++) {
                    int ny = y + dy;
                    for (int dx = -5; dx <= 5; dx++) {
                        int nx = x + dx;
                        int other;
                        if (nx < 0 || nx >= FONT_W || ny < 0 || ny >= FONT_H)
                            other = 0; /* outside font bounds = outside glyph */
                        else
                            other = font_texel((unsigned char)ch, nx, ny);

                        if (other != inside) {
                            int d2 = dx * dx + dy * dy;
                            if (d2 < min_d2) min_d2 = d2;
                        }
                    }
                }

                /* Distance in 1/8-texel units: (sqrt(d2) - 0.5) * 8
                   The -0.5 accounts for the edge being between texel centers */
                int dist8;
                if (min_d2 >= 9999)
                    dist8 = 64; /* far from any edge */
                else {
                    dist8 = isqrt_int(min_d2 * 64) - 4;
                    if (dist8 < 0) dist8 = 0;
                }

                int sd = inside ? dist8 : -dist8;
                if (sd > 127) sd = 127;
                if (sd < -128) sd = -128;
                font_sdf[ch][y][x] = (int8_t)sd;
            }
        }
    }
}

static inline int sdf_texel(unsigned char c, int x, int y) {
    if (x < 0 || x >= FONT_W || y < 0 || y >= FONT_H) return -32;
    return font_sdf[c][y][x];
}

void gfx_surf_draw_char_smooth(gfx_surface_t *s, int px, int py, char c, uint32_t fg, int sc) {
    unsigned char uc = (unsigned char)c;
    int out_w = FONT_W * sc;
    int out_h = FONT_H * sc;

    /* Sharpness: controls edge width in output pixels.
       Higher = sharper. Tuned for ~1.5 pixel AA transition. */
    int sharpness = 255 * sc / 12;
    if (sharpness < 20) sharpness = 20;

    for (int oy = 0; oy < out_h; oy++) {
        int fy256 = (oy * 256 + 128) / sc - 128;
        int iy = fy256 >> 8;
        int fracy = fy256 & 0xFF;

        for (int ox = 0; ox < out_w; ox++) {
            int fx256 = (ox * 256 + 128) / sc - 128;
            int ix = fx256 >> 8;
            int fracx = fx256 & 0xFF;

            /* Bilinear interpolation of SDF */
            int d00 = sdf_texel(uc, ix,     iy);
            int d10 = sdf_texel(uc, ix + 1, iy);
            int d01 = sdf_texel(uc, ix,     iy + 1);
            int d11 = sdf_texel(uc, ix + 1, iy + 1);

            /* Quick reject: all far outside */
            if (d00 < -16 && d10 < -16 && d01 < -16 && d11 < -16) continue;

            int top = d00 * (256 - fracx) + d10 * fracx;
            int bot = d01 * (256 - fracx) + d11 * fracx;
            int val = top * (256 - fracy) + bot * fracy;
            int dist = val >> 16; /* back to SDF units */

            int alpha = dist * sharpness + 128;
            if (alpha > 255) alpha = 255;
            if (alpha <= 0) continue;

            int dx = px + ox, dy = py + oy;
            if (alpha >= 250)
                gfx_surf_put_pixel(s, dx, dy, fg);
            else
                gfx_surf_blend_pixel(s, dx, dy, fg, (uint8_t)alpha);
        }
    }
}

void gfx_surf_draw_string_smooth(gfx_surface_t *s, int px, int py, const char *str, uint32_t fg, int sc) {
    while (*str) {
        gfx_surf_draw_char_smooth(s, px, py, *str, fg, sc);
        px += FONT_W * sc;
        str++;
    }
}

/* ═══ Backbuffer convenience wrappers (geometry) ══════════════ */

void gfx_fill_circle(int cx, int cy, int r, uint32_t color) {
    gfx_surface_t s = gfx_get_surface();
    gfx_surf_fill_circle(&s, cx, cy, r, color);
}

void gfx_fill_circle_aa(int cx, int cy, int r, uint32_t color) {
    gfx_surface_t s = gfx_get_surface();
    gfx_surf_fill_circle_aa(&s, cx, cy, r, color);
}

void gfx_circle_ring(int cx, int cy, int r, int thick, uint32_t color) {
    gfx_surface_t s = gfx_get_surface();
    gfx_surf_circle_ring(&s, cx, cy, r, thick, color);
}

void gfx_rounded_rect(int x, int y, int w, int h, int r, uint32_t color) {
    gfx_surface_t s = gfx_get_surface();
    gfx_surf_rounded_rect(&s, x, y, w, h, r, color);
}

void gfx_rounded_rect_alpha(int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha) {
    gfx_surface_t s = gfx_get_surface();
    gfx_surf_rounded_rect_alpha(&s, x, y, w, h, r, color, alpha);
}

void gfx_rounded_rect_outline(int x, int y, int w, int h, int r, uint32_t color) {
    gfx_surface_t s = gfx_get_surface();
    gfx_surf_rounded_rect_outline(&s, x, y, w, h, r, color);
}

void gfx_draw_char_scaled(int x, int y, char c, uint32_t fg, int scale) {
    gfx_surface_t s = gfx_get_surface();
    gfx_surf_draw_char_scaled(&s, x, y, c, fg, scale);
}

void gfx_draw_string_scaled(int x, int y, const char *str, uint32_t fg, int scale) {
    gfx_surface_t s = gfx_get_surface();
    gfx_surf_draw_string_scaled(&s, x, y, str, fg, scale);
}

void gfx_draw_char_smooth(int x, int y, char c, uint32_t fg, int scale) {
    gfx_surface_t s = gfx_get_surface();
    gfx_surf_draw_char_smooth(&s, x, y, c, fg, scale);
}

void gfx_draw_string_smooth(int x, int y, const char *str, uint32_t fg, int scale) {
    gfx_surface_t s = gfx_get_surface();
    gfx_surf_draw_string_smooth(&s, x, y, str, fg, scale);
}

int gfx_string_scaled_w(const char *str, int scale) {
    return (int)strlen(str) * FONT_W * scale;
}

/* ═══ Backbuffer pixel primitives (delegate to surface) ═══════ */

void gfx_put_pixel(int x, int y, uint32_t color) {
    gfx_surface_t s = gfx_get_surface();
    gfx_surf_put_pixel(&s, x, y, color);
}

void gfx_fill_rect(int x, int y, int w, int h, uint32_t color) {
    gfx_surface_t s = gfx_get_surface();
    gfx_surf_fill_rect(&s, x, y, w, h, color);
}

void gfx_draw_rect(int x, int y, int w, int h, uint32_t color) {
    gfx_surface_t s = gfx_get_surface();
    gfx_surf_draw_rect(&s, x, y, w, h, color);
}

void gfx_draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
    gfx_surface_t s = gfx_get_surface();
    gfx_surf_draw_line(&s, x0, y0, x1, y1, color);
}

void gfx_clear(uint32_t color) {
    if (color == 0) {
        memset(backbuf, 0, fb_height * fb_pitch);
        return;
    }
    uint32_t pitch4 = fb_pitch / 4;
    /* Fill first row */
    uint32_t *first = backbuf;
    for (uint32_t x = 0; x < fb_width; x++)
        first[x] = color;
    /* Replicate to remaining rows */
    for (uint32_t y = 1; y < fb_height; y++)
        memcpy(backbuf + y * pitch4, first, fb_width * 4);
}

/* ═══ Buffer-targeted drawing (legacy — delegate to surface) ═══ */

void gfx_buf_put_pixel(uint32_t *buf, int bw, int bh, int x, int y, uint32_t color) {
    gfx_surface_t s = { buf, bw, bh, bw };
    gfx_surf_put_pixel(&s, x, y, color);
}

void gfx_buf_fill_rect(uint32_t *buf, int bw, int bh, int x, int y, int w, int h, uint32_t color) {
    gfx_surface_t s = { buf, bw, bh, bw };
    gfx_surf_fill_rect(&s, x, y, w, h, color);
}

void gfx_buf_draw_rect(uint32_t *buf, int bw, int bh, int x, int y, int w, int h, uint32_t color) {
    gfx_surface_t s = { buf, bw, bh, bw };
    gfx_surf_draw_rect(&s, x, y, w, h, color);
}

void gfx_buf_draw_line(uint32_t *buf, int bw, int bh, int x0, int y0, int x1, int y1, uint32_t color) {
    gfx_surface_t s = { buf, bw, bh, bw };
    gfx_surf_draw_line(&s, x0, y0, x1, y1, color);
}

void gfx_buf_draw_char(uint32_t *buf, int bw, int bh, int px, int py, char c, uint32_t fg, uint32_t bg) {
    gfx_surface_t s = { buf, bw, bh, bw };
    gfx_surf_draw_char(&s, px, py, c, fg, bg);
}

void gfx_buf_draw_string(uint32_t *buf, int bw, int bh, int px, int py, const char *str, uint32_t fg, uint32_t bg) {
    gfx_surface_t s = { buf, bw, bh, bw };
    gfx_surf_draw_string(&s, px, py, str, fg, bg);
}

void gfx_blit_buffer(int dst_x, int dst_y, uint32_t *src, int sw, int sh) {
    if (!src) return;
    uint32_t pitch4 = fb_pitch / 4;
    /* Clip source region to screen */
    int sx0 = 0, sy0 = 0;
    int dx = dst_x, dy = dst_y;
    int w = sw, h = sh;
    if (dx < 0) { sx0 = -dx; w += dx; dx = 0; }
    if (dy < 0) { sy0 = -dy; h += dy; dy = 0; }
    if (dx + w > (int)fb_width) w = (int)fb_width - dx;
    if (dy + h > (int)fb_height) h = (int)fb_height - dy;
    if (w <= 0 || h <= 0) return;
    for (int row = 0; row < h; row++) {
        memcpy(backbuf + (dy + row) * pitch4 + dx,
               src + (sy0 + row) * sw + sx0,
               (size_t)w * 4);
    }
}

/* ═══ Alpha blending (backbuffer) ══════════════════════════════ */

void gfx_blend_pixel(int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || (uint32_t)x >= fb_width || (uint32_t)y >= fb_height)
        return;
    uint32_t idx = y * (fb_pitch / 4) + x;
    backbuf[idx] = alpha_blend(backbuf[idx], color);
}

void gfx_fill_rect_alpha(int x, int y, int w, int h, uint32_t color) {
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
            dst[col] = alpha_blend(dst[col], color);
    }
}

void gfx_draw_char_alpha(int px, int py, char c, uint32_t fg_with_alpha) {
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
                if (xx >= 0 && (uint32_t)xx < fb_width) {
                    uint32_t idx = yy * pitch4 + xx;
                    backbuf[idx] = alpha_blend(backbuf[idx], fg_with_alpha);
                }
            }
        }
    }
}

/* ═══ Mouse cursor rendering ══════════════════════════════════ */

#define CURSOR_W 12
#define CURSOR_H 16

/* Arrow cursor (default) — hotspot (0,0) */
static const uint8_t arrow_bitmap[CURSOR_H] = {
    0x80, 0xC0, 0xE0, 0xF0,
    0xF8, 0xFC, 0xFE, 0xFF,
    0xFC, 0xF8, 0xF0, 0xD0,
    0x88, 0x08, 0x04, 0x04,
};
static const uint8_t arrow_mask[CURSOR_H] = {
    0xC0, 0xE0, 0xF0, 0xF8,
    0xFC, 0xFE, 0xFF, 0xFF,
    0xFE, 0xFC, 0xF8, 0xF8,
    0xCC, 0x0C, 0x06, 0x06,
};

/* Hand cursor — hotspot (3,0) */
static const uint8_t hand_bitmap[CURSOR_H] = {
    0x08, 0x18, 0x18, 0x18,  /* finger tip */
    0x18, 0x58, 0xDA, 0xDE,  /* other fingers */
    0x7E, 0x7E, 0x3E, 0x3C,  /* palm */
    0x3C, 0x1C, 0x18, 0x00,
};
static const uint8_t hand_mask[CURSOR_H] = {
    0x1C, 0x3C, 0x3C, 0x3C,
    0x3C, 0xFE, 0xFF, 0xFF,
    0xFF, 0xFF, 0x7F, 0x7E,
    0x7E, 0x3E, 0x3C, 0x18,
};

/* Text I-beam cursor — hotspot (3,8) */
static const uint8_t text_bitmap[CURSOR_H] = {
    0x6C, 0x38, 0x18, 0x18,  /* top serifs + stem */
    0x18, 0x18, 0x18, 0x18,
    0x18, 0x18, 0x18, 0x18,
    0x18, 0x38, 0x6C, 0x00,  /* bottom serifs */
};
static const uint8_t text_mask[CURSOR_H] = {
    0xFE, 0x7C, 0x3C, 0x3C,
    0x3C, 0x3C, 0x3C, 0x3C,
    0x3C, 0x3C, 0x3C, 0x3C,
    0x3C, 0x7C, 0xFE, 0x00,
};

typedef struct {
    const uint8_t *bitmap;
    const uint8_t *mask;
    int hotspot_x, hotspot_y;
} cursor_shape_t;

static const cursor_shape_t cursor_shapes[] = {
    { arrow_bitmap, arrow_mask, 0, 0 },   /* ARROW */
    { hand_bitmap,  hand_mask,  3, 0 },   /* HAND */
    { text_bitmap,  text_mask,  3, 8 },   /* TEXT */
};

static int current_cursor_type = 0;
static uint32_t cursor_save[CURSOR_W * CURSOR_H];
static int cursor_saved_x = -1, cursor_saved_y = -1;
static int cursor_visible = 0;

void gfx_set_cursor_type(int type) {
    if (type >= 0 && type <= 2)
        current_cursor_type = type;
}

int gfx_get_cursor_type(void) {
    return current_cursor_type;
}

void gfx_draw_mouse_cursor(int x, int y) {
    if (!gfx_active) return;
    uint32_t pitch4 = fb_pitch / 4;

    /* Restore previous position first */
    if (cursor_visible)
        gfx_restore_mouse_cursor();

    const cursor_shape_t *cs = &cursor_shapes[current_cursor_type];
    int draw_x = x - cs->hotspot_x;
    int draw_y = y - cs->hotspot_y;

    /* Save pixels under cursor */
    for (int row = 0; row < CURSOR_H; row++) {
        int yy = draw_y + row;
        if (yy < 0 || (uint32_t)yy >= fb_height) {
            for (int col = 0; col < CURSOR_W; col++)
                cursor_save[row * CURSOR_W + col] = 0;
            continue;
        }
        for (int col = 0; col < CURSOR_W; col++) {
            int xx = draw_x + col;
            if (xx < 0 || (uint32_t)xx >= fb_width)
                cursor_save[row * CURSOR_W + col] = 0;
            else
                cursor_save[row * CURSOR_W + col] = framebuffer[yy * pitch4 + xx];
        }
    }
    cursor_saved_x = draw_x;
    cursor_saved_y = draw_y;
    cursor_visible = 1;

    /* Draw cursor to framebuffer */
    for (int row = 0; row < CURSOR_H; row++) {
        int yy = draw_y + row;
        if (yy < 0 || (uint32_t)yy >= fb_height) continue;
        uint8_t mask_bits = cs->mask[row];
        uint8_t bmp_bits  = cs->bitmap[row];
        for (int col = 0; col < 8; col++) {
            int xx = draw_x + col;
            if (xx < 0 || (uint32_t)xx >= fb_width) continue;
            if (mask_bits & (0x80 >> col)) {
                uint32_t c = (bmp_bits & (0x80 >> col)) ? GFX_WHITE : GFX_BLACK;
                framebuffer[yy * pitch4 + xx] = c;
            }
        }
    }
}

void gfx_restore_mouse_cursor(void) {
    if (!cursor_visible || cursor_saved_x < 0) return;
    uint32_t pitch4 = fb_pitch / 4;
    for (int row = 0; row < CURSOR_H; row++) {
        int yy = cursor_saved_y + row;
        if (yy < 0 || (uint32_t)yy >= fb_height) continue;
        for (int col = 0; col < CURSOR_W; col++) {
            int xx = cursor_saved_x + col;
            if (xx < 0 || (uint32_t)xx >= fb_width) continue;
            framebuffer[yy * pitch4 + xx] = cursor_save[row * CURSOR_W + col];
        }
    }
    cursor_visible = 0;
}

/* ═══ Text rendering (backbuffer) ═════════════════════════════ */

void gfx_draw_char(int px, int py, char c, uint32_t fg, uint32_t bg) {
    gfx_surface_t s = gfx_get_surface();
    gfx_surf_draw_char(&s, px, py, c, fg, bg);
}

void gfx_draw_string(int px, int py, const char* str, uint32_t fg, uint32_t bg) {
    gfx_surface_t s = gfx_get_surface();
    gfx_surf_draw_string(&s, px, py, str, fg, bg);
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

void gfx_draw_string_nobg(int px, int py, const char* str, uint32_t fg) {
    while (*str) {
        gfx_draw_char_nobg(px, py, *str, fg);
        px += FONT_W;
        str++;
    }
}

void gfx_putchar_at(int col, int row, unsigned char c, uint32_t fg, uint32_t bg) {
    gfx_draw_char(col * FONT_W, row * FONT_H, (char)c, fg, bg);
}

/* ═══ Cursor ══════════════════════════════════════════════════ */

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

/* ═══ Double buffering ════════════════════════════════════════ */

void gfx_flip(void) {
    if (!have_backbuffer) return;
    /* Remove cursor before overwriting framebuffer so cursor_save stays valid */
    gfx_restore_mouse_cursor();
    /* Scanline-diff: only copy lines that actually changed.
       Writes to MMIO framebuffer are slow; skipping unchanged lines is a big win. */
    uint32_t pitch4 = fb_pitch / 4;
    size_t row_bytes = fb_width * 4;
    for (uint32_t y = 0; y < fb_height; y++) {
        uint32_t off = y * pitch4;
        if (memcmp(backbuf + off, framebuffer + off, row_bytes) != 0)
            memcpy(framebuffer + off, backbuf + off, row_bytes);
    }
    /* Re-save pixels under cursor from fresh framebuffer content and redraw */
    gfx_draw_mouse_cursor(mouse_get_x(), mouse_get_y());
}

void gfx_flip_rect(int x, int y, int w, int h) {
    if (!have_backbuffer) return;

    /* Clip */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)fb_width) w = (int)fb_width - x;
    if (y + h > (int)fb_height) h = (int)fb_height - y;
    if (w <= 0 || h <= 0) return;

    /* Remove cursor before overwriting framebuffer so cursor_save stays valid */
    gfx_restore_mouse_cursor();

    uint32_t pitch4 = fb_pitch / 4;
    for (int row = y; row < y + h; row++) {
        memcpy(&framebuffer[row * pitch4 + x],
               &backbuf[row * pitch4 + x],
               (size_t)w * 4);
    }

    /* Re-save pixels under cursor from fresh framebuffer content and redraw */
    gfx_draw_mouse_cursor(mouse_get_x(), mouse_get_y());
}

void gfx_overlay_darken(int x, int y, int w, int h, uint8_t alpha) {
    if (!have_backbuffer) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)fb_width) w = (int)fb_width - x;
    if (y + h > (int)fb_height) h = (int)fb_height - y;
    if (w <= 0 || h <= 0 || alpha == 0) return;

    uint32_t inv_a = 255 - (uint32_t)alpha;
    uint32_t pitch4 = fb_pitch / 4;
    for (int row = y; row < y + h; row++) {
        uint32_t *dst = backbuf + row * pitch4 + x;
        for (int col = 0; col < w; col++) {
            uint32_t px = dst[col];
            uint32_t r = ((px >> 16) & 0xFF) * inv_a / 255;
            uint32_t g = ((px >> 8) & 0xFF) * inv_a / 255;
            uint32_t b = (px & 0xFF) * inv_a / 255;
            dst[col] = (r << 16) | (g << 8) | b;
        }
    }
}

void gfx_crossfade(int steps, int delay_ms) {
    if (!have_backbuffer || steps <= 0) { gfx_flip(); return; }

    uint32_t total = fb_height * (fb_pitch / 4);

    /* Save the old scene (current framebuffer) */
    uint32_t *saved = (uint32_t *)malloc(total * sizeof(uint32_t));
    if (!saved) { gfx_flip(); return; }
    memcpy(saved, framebuffer, total * sizeof(uint32_t));

    /* Blend old (saved) → new (backbuf), write to framebuffer */
    for (int i = 1; i <= steps; i++) {
        uint32_t t = (uint32_t)(i * 255 / steps);
        uint32_t inv_t = 255 - t;
        for (uint32_t j = 0; j < total; j++) {
            uint32_t src = saved[j];
            uint32_t dst = backbuf[j];
            uint32_t r = (((src >> 16) & 0xFF) * inv_t + ((dst >> 16) & 0xFF) * t) / 255;
            uint32_t g = (((src >> 8) & 0xFF) * inv_t + ((dst >> 8) & 0xFF) * t) / 255;
            uint32_t b = ((src & 0xFF) * inv_t + (dst & 0xFF) * t) / 255;
            framebuffer[j] = (r << 16) | (g << 8) | b;
        }
        pit_sleep_ms(delay_ms);
    }

    free(saved);
}
