#include <kernel/compositor.h>
#include <kernel/gfx.h>
#include <kernel/libdrm.h>
#include <kernel/drm.h>
#include <kernel/ui_theme.h>
#include <kernel/idt.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define COMP_MAX_SURFACES   64
#define COMP_MAX_PER_LAYER  16
#define FRAME_TICKS          2   /* 120Hz / 2 = 60fps */

static comp_surface_t pool[COMP_MAX_SURFACES];

/* layers[L][0] is back, layers[L][count-1] is front */
static int  layer_idx[COMP_LAYER_COUNT][COMP_MAX_PER_LAYER];
static int  layer_count[COMP_LAYER_COUNT];

static uint32_t last_frame_tick = 0;

static uint32_t fps_frame_count = 0;
static uint32_t fps_last_tick   = 0;
static uint32_t fps_value       = 0;

static int  screen_dirty = 0;
static int  sdx, sdy, sdw, sdh;

/* ── DRM-backed compositing state ──────────────────────────────── */
static int      drm_fd       = -1;
static uint32_t drm_gem_handle = 0;
static uint32_t drm_fb_id    = 0;
static uint32_t drm_crtc_id  = 0;
static int      drm_active   = 0;   /* 1 = compositor using DRM buffers */

static void rect_union(int *dx, int *dy, int *dw, int *dh,
                       int ax, int ay, int aw, int ah) {
    if (*dw == 0 || *dh == 0) {
        *dx = ax; *dy = ay; *dw = aw; *dh = ah; return;
    }
    int x2 = *dx + *dw, y2 = *dy + *dh;
    int bx2 = ax + aw,  by2 = ay + ah;
    if (ax < *dx) *dx = ax;
    if (ay < *dy) *dy = ay;
    if (bx2 > x2) x2 = bx2;
    if (by2 > y2) y2 = by2;
    *dw = x2 - *dx;
    *dh = y2 - *dy;
}

static void rect_clamp(int *x, int *y, int *w, int *h, int sw, int sh) {
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x + *w > sw) *w = sw - *x;
    if (*y + *h > sh) *h = sh - *y;
    if (*w < 0) *w = 0;
    if (*h < 0) *h = 0;
}

static int intersect_with_dirty(comp_surface_t *s,
                                 int *blit_sx, int *blit_sy,
                                 int *blit_dx, int *blit_dy,
                                 int *blit_w,  int *blit_h)
{
    int sx = s->screen_x, sy = s->screen_y, sw = s->w, sh = s->h;

    int ix = (sx > sdx) ? sx : sdx;
    int iy = (sy > sdy) ? sy : sdy;
    int ix2 = (sx + sw < sdx + sdw) ? sx + sw : sdx + sdw;
    int iy2 = (sy + sh < sdy + sdh) ? sy + sh : sdy + sdh;

    if (ix >= ix2 || iy >= iy2) return 0;

    *blit_dx = ix;
    *blit_dy = iy;
    *blit_w  = ix2 - ix;
    *blit_h  = iy2 - iy;
    *blit_sx = ix - sx;
    *blit_sy = iy - sy;
    return 1;
}

/* src: 0xAARRGGBB, surf_alpha: global opacity (255=opaque) */
static inline uint32_t blend_pixel(uint32_t dst, uint32_t src, uint8_t surf_alpha) {
    uint32_t sa = (src >> 24) & 0xFF;
    uint32_t a  = (sa * surf_alpha) >> 8;
    if (a == 0)   return dst;
    if (a == 255) return src & 0xFFFFFF;

    uint32_t sr = (src >> 16) & 0xFF, sg = (src >> 8) & 0xFF, sb = src & 0xFF;
    uint32_t dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
    uint32_t ia = 255 - a;
    uint32_t r = (sr * a + dr * ia) >> 8;
    uint32_t g = (sg * a + dg * ia) >> 8;
    uint32_t b = (sb * a + db * ia) >> 8;
    return (r << 16) | (g << 8) | b;
}

static void blit_surface_region(comp_surface_t *s,
                                 int src_x, int src_y,
                                 int dst_x, int dst_y,
                                 int bw, int bh) {
    if (bw <= 0 || bh <= 0) return;

    uint32_t *bb     = gfx_backbuffer();
    uint32_t  pitch4 = gfx_pitch() / 4;
    uint8_t   alpha  = s->alpha;

    for (int row = 0; row < bh; row++) {
        uint32_t *src_row = s->pixels + (src_y + row) * s->w + src_x;
        uint32_t *dst_row = bb + (dst_y + row) * pitch4 + dst_x;

        if (alpha == 255) {
            int all_opaque = 1;
            for (int col = 0; col < bw; col++) {
                if ((src_row[col] >> 24) != 0xFF) { all_opaque = 0; break; }
            }
            if (all_opaque) {
                memcpy(dst_row, src_row, (size_t)bw * 4);
                continue;
            }
        }

        for (int col = 0; col < bw; col++)
            dst_row[col] = blend_pixel(dst_row[col], src_row[col], alpha);
    }
}

static void bb_fill_rect(int x, int y, int w, int h, uint32_t color) {
    uint32_t *bb     = gfx_backbuffer();
    uint32_t  pitch4 = gfx_pitch() / 4;
    int       sw     = (int)gfx_width();
    int       sh     = (int)gfx_height();
    rect_clamp(&x, &y, &w, &h, sw, sh);
    for (int row = 0; row < h; row++) {
        uint32_t *p = bb + (y + row) * pitch4 + x;
        for (int col = 0; col < w; col++) p[col] = color;
    }
}

comp_surface_t *comp_surface_create(int w, int h, int layer) {
    if (layer < 0 || layer >= COMP_LAYER_COUNT) return 0;
    if (layer_count[layer] >= COMP_MAX_PER_LAYER) return 0;

    comp_surface_t *s = 0;
    for (int i = 0; i < COMP_MAX_SURFACES; i++) {
        if (!pool[i].in_use) { s = &pool[i]; break; }
    }
    if (!s) return 0;

    s->pixels = (uint32_t *)malloc((size_t)w * h * 4);
    if (!s->pixels) return 0;
    memset(s->pixels, 0, (size_t)w * h * 4);

    s->w         = w;
    s->h         = h;
    s->screen_x  = 0;
    s->screen_y  = 0;
    s->alpha     = 255;
    s->visible   = 1;
    s->layer     = (uint8_t)layer;
    s->in_use    = 1;
    s->damage_all = 1;
    s->dmg_x = s->dmg_y = s->dmg_w = s->dmg_h = 0;

    layer_idx[layer][layer_count[layer]++] = (int)(s - pool);
    return s;
}

void comp_surface_destroy(comp_surface_t *s) {
    if (!s || !s->in_use) return;

    rect_union(&sdx, &sdy, &sdw, &sdh,
               s->screen_x, s->screen_y, s->w, s->h);
    screen_dirty = 1;

    free(s->pixels);
    s->pixels = 0;
    int pool_idx = (int)(s - pool);

    int L = s->layer;
    for (int i = 0; i < layer_count[L]; i++) {
        if (layer_idx[L][i] == pool_idx) {
            for (int j = i; j < layer_count[L] - 1; j++)
                layer_idx[L][j] = layer_idx[L][j + 1];
            layer_count[L]--;
            break;
        }
    }
    s->in_use = 0;
}

void comp_surface_move(comp_surface_t *s, int x, int y) {
    if (!s || !s->in_use) return;
    if (s->screen_x == x && s->screen_y == y) return;
    rect_union(&sdx, &sdy, &sdw, &sdh,
               s->screen_x, s->screen_y, s->w, s->h);
    s->screen_x = x;
    s->screen_y = y;
    rect_union(&sdx, &sdy, &sdw, &sdh, x, y, s->w, s->h);
    screen_dirty = 1;
}

int comp_surface_resize(comp_surface_t *s, int new_w, int new_h) {
    if (!s || !s->in_use) return 0;
    if (s->w == new_w && s->h == new_h) return 1;
    rect_union(&sdx, &sdy, &sdw, &sdh,
               s->screen_x, s->screen_y, s->w, s->h);
    uint32_t *np = (uint32_t *)malloc((size_t)new_w * new_h * 4);
    if (!np) return 0;
    memset(np, 0, (size_t)new_w * new_h * 4);
    free(s->pixels);
    s->pixels = np;
    s->w = new_w;
    s->h = new_h;
    s->damage_all = 1;
    rect_union(&sdx, &sdy, &sdw, &sdh,
               s->screen_x, s->screen_y, new_w, new_h);
    screen_dirty = 1;
    return 1;
}

void comp_surface_set_alpha(comp_surface_t *s, uint8_t alpha) {
    if (!s || !s->in_use) return;
    s->alpha = alpha;
    comp_surface_damage_all(s);
}

void comp_surface_set_visible(comp_surface_t *s, int visible) {
    if (!s || !s->in_use) return;
    s->visible = (uint8_t)visible;
    rect_union(&sdx, &sdy, &sdw, &sdh,
               s->screen_x, s->screen_y, s->w, s->h);
    screen_dirty = 1;
}

void comp_surface_raise(comp_surface_t *s) {
    if (!s || !s->in_use) return;
    int L = s->layer, pi = (int)(s - pool);
    for (int i = 0; i < layer_count[L]; i++) {
        if (layer_idx[L][i] == pi) {
            for (int j = i; j < layer_count[L] - 1; j++)
                layer_idx[L][j] = layer_idx[L][j + 1];
            layer_idx[L][layer_count[L] - 1] = pi;
            break;
        }
    }
    comp_surface_damage_all(s);
}

void comp_surface_lower(comp_surface_t *s) {
    if (!s || !s->in_use) return;
    int L = s->layer, pi = (int)(s - pool);
    for (int i = 0; i < layer_count[L]; i++) {
        if (layer_idx[L][i] == pi) {
            for (int j = i; j > 0; j--)
                layer_idx[L][j] = layer_idx[L][j - 1];
            layer_idx[L][0] = pi;
            break;
        }
    }
    comp_surface_damage_all(s);
}

void comp_surface_damage(comp_surface_t *s, int x, int y, int w, int h) {
    if (!s || !s->in_use || !s->visible) return;
    if (s->damage_all) return;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > s->w) w = s->w - x;
    if (y + h > s->h) h = s->h - y;
    if (w <= 0 || h <= 0) return;

    if (s->dmg_w == 0 || s->dmg_h == 0) {
        s->dmg_x = x; s->dmg_y = y; s->dmg_w = w; s->dmg_h = h;
    } else {
        rect_union(&s->dmg_x, &s->dmg_y, &s->dmg_w, &s->dmg_h, x, y, w, h);
    }

    rect_union(&sdx, &sdy, &sdw, &sdh,
               s->screen_x + x, s->screen_y + y, w, h);
    screen_dirty = 1;
}

void comp_surface_damage_all(comp_surface_t *s) {
    if (!s || !s->in_use) return;
    s->damage_all = 1;
    rect_union(&sdx, &sdy, &sdw, &sdh,
               s->screen_x, s->screen_y, s->w, s->h);
    screen_dirty = 1;
}

gfx_surface_t comp_surface_lock(comp_surface_t *s) {
    gfx_surface_t gs;
    gs.buf   = s ? s->pixels : 0;
    gs.w     = s ? s->w : 0;
    gs.h     = s ? s->h : 0;
    gs.pitch = s ? s->w : 0;
    return gs;
}

void comp_surf_fill_rect(comp_surface_t *s, int x, int y, int w, int h, uint32_t color) {
    if (!s || !s->in_use) return;
    gfx_surface_t gs = comp_surface_lock(s);
    uint32_t c = (color & 0xFFFFFF) | 0xFF000000;
    gfx_surf_fill_rect(&gs, x, y, w, h, c);
    comp_surface_damage(s, x, y, w, h);
}

void comp_surf_draw_string(comp_surface_t *s, int x, int y,
                            const char *str, uint32_t fg, uint32_t bg) {
    if (!s || !s->in_use) return;
    gfx_surface_t gs = comp_surface_lock(s);
    gfx_surf_draw_string(&gs, x, y, str, fg, bg);
    int len = 0; while (str[len]) len++;
    comp_surface_damage(s, x, y, len * FONT_W, FONT_H);
}

void comp_surf_clear(comp_surface_t *s, uint32_t color) {
    if (!s || !s->in_use) return;
    uint32_t c = (color & 0xFFFFFF) | 0xFF000000;
    for (int i = 0; i < s->w * s->h; i++) s->pixels[i] = c;
    comp_surface_damage_all(s);
}

void compositor_init(void) {
    memset(pool,        0, sizeof(pool));
    memset(layer_idx,   0, sizeof(layer_idx));
    memset(layer_count, 0, sizeof(layer_count));
    last_frame_tick  = 0;
    fps_frame_count  = 0;
    fps_last_tick    = 0;
    fps_value        = 0;
    screen_dirty     = 1;
    sdx = sdy = 0;
    sdw = (int)gfx_width();
    sdh = (int)gfx_height();

    /* ── DRM-backed compositing buffer ──────────────────────── */
    drm_active = 0;
    drm_fd = drmOpen("impos-drm", NULL);
    if (drm_fd < 0) return;

    uint32_t w = gfx_width(), h = gfx_height();
    uint32_t pitch = 0;
    uint64_t size = 0;

    if (drmModeCreateDumbBuffer(drm_fd, w, h, 32, 0,
                                &drm_gem_handle, &pitch, &size) != 0) {
        drmClose(drm_fd); drm_fd = -1;
        return;
    }

    /* Map the GEM buffer to get its address (identity-mapped) */
    uint64_t offset = 0;
    if (drmModeMapDumbBuffer(drm_fd, drm_gem_handle, &offset) != 0) {
        drmModeDestroyDumbBuffer(drm_fd, drm_gem_handle);
        drmClose(drm_fd); drm_fd = -1;
        return;
    }

    /* Register as a DRM framebuffer */
    if (drmModeAddFB(drm_fd, w, h, 24, 32, pitch,
                     drm_gem_handle, &drm_fb_id) != 0) {
        drmModeDestroyDumbBuffer(drm_fd, drm_gem_handle);
        drmClose(drm_fd); drm_fd = -1;
        return;
    }

    /* Get the CRTC id */
    drmModeResPtr res = drmModeGetResources(drm_fd);
    if (res && res->count_crtcs > 0) {
        drm_crtc_id = res->crtcs[0];
        drmModeFreeResources(res);
    } else {
        if (res) drmModeFreeResources(res);
        drmModeRmFB(drm_fd, drm_fb_id);
        drmModeDestroyDumbBuffer(drm_fd, drm_gem_handle);
        drmClose(drm_fd); drm_fd = -1;
        return;
    }

    /* Point the gfx backbuffer at the GEM buffer — zero-copy compositing */
    uint32_t *gem_ptr = (uint32_t *)(uint32_t)offset;
    memset(gem_ptr, 0, (size_t)size);
    gfx_set_backbuffer(gem_ptr);

    drm_active = 1;
    printf("[COMP] DRM-backed compositing active (GEM handle=%u, fb=%u)\n",
           drm_gem_handle, drm_fb_id);
}

void compositor_damage_all(void) {
    for (int i = 0; i < COMP_MAX_SURFACES; i++) {
        if (pool[i].in_use) pool[i].damage_all = 1;
    }
    screen_dirty = 1;
    sdx = sdy = 0;
    sdw = (int)gfx_width();
    sdh = (int)gfx_height();
}

void compositor_frame(void) {
    if (!gfx_is_active()) return;

    uint32_t now = pit_get_ticks();

    if (!screen_dirty) goto fps_update;

    {
        int sw = (int)gfx_width(), sh = (int)gfx_height();
        rect_clamp(&sdx, &sdy, &sdw, &sdh, sw, sh);
        if (sdw <= 0 || sdh <= 0) { screen_dirty = 0; goto fps_update; }
    }

    /* 1. Clear dirty region to desktop background.
       Skip if wallpaper covers the entire dirty rect (it's opaque, full-screen). */
    {
        int need_clear = 1;
        if (layer_count[COMP_LAYER_WALLPAPER] > 0) {
            comp_surface_t *wp = &pool[layer_idx[COMP_LAYER_WALLPAPER][0]];
            if (wp->in_use && wp->visible && wp->alpha == 255 &&
                wp->screen_x <= sdx && wp->screen_y <= sdy &&
                wp->screen_x + wp->w >= sdx + sdw &&
                wp->screen_y + wp->h >= sdy + sdh)
                need_clear = 0;
        }
        if (need_clear)
            bb_fill_rect(sdx, sdy, sdw, sdh, ui_theme.desktop_bg);
    }

    for (int L = 0; L < COMP_LAYER_COUNT; L++) {
        for (int i = 0; i < layer_count[L]; i++) {
            comp_surface_t *s = &pool[layer_idx[L][i]];
            if (!s->in_use || !s->visible) continue;

            int blit_sx, blit_sy, blit_dx, blit_dy, blit_w, blit_h;
            if (!intersect_with_dirty(s,
                                      &blit_sx, &blit_sy,
                                      &blit_dx, &blit_dy,
                                      &blit_w, &blit_h)) continue;

            blit_surface_region(s, blit_sx, blit_sy,
                                   blit_dx, blit_dy,
                                   blit_w,  blit_h);
        }
    }

    /* Flip the dirty region to display.
     * When DRM is active, backbuffer IS the GEM buffer (zero-copy).
     * gfx_flip_rect still handles the backbuf → framebuffer transfer. */
    gfx_flip_rect(sdx, sdy, sdw, sdh);

    for (int i = 0; i < COMP_MAX_SURFACES; i++) {
        if (!pool[i].in_use) continue;
        pool[i].damage_all = 0;
        pool[i].dmg_x = pool[i].dmg_y = pool[i].dmg_w = pool[i].dmg_h = 0;
    }
    screen_dirty = 0;
    sdx = sdy = sdw = sdh = 0;

fps_update:
    fps_frame_count++;
    if (now - fps_last_tick >= 120) {
        fps_value       = fps_frame_count;
        fps_frame_count = 0;
        fps_last_tick   = now;
    }
}

uint32_t compositor_get_fps(void) { return fps_value; }

#define COMP_CURSOR_W  12
#define COMP_CURSOR_H  16

static comp_surface_t *cursor_surf = 0;
static int cursor_type_drawn = -1;

void comp_cursor_init(void) {
    if (gfx_using_virtio_gpu()) return;
    cursor_surf = comp_surface_create(COMP_CURSOR_W, COMP_CURSOR_H,
                                       COMP_LAYER_CURSOR);
    if (!cursor_surf) return;
    gfx_render_cursor_to_buffer(cursor_surf->pixels,
                                 COMP_CURSOR_W, COMP_CURSOR_H);
    cursor_type_drawn = gfx_get_cursor_type();
    comp_surface_damage_all(cursor_surf);
}

void comp_cursor_move(int x, int y) {
    if (gfx_using_virtio_gpu()) {
        gfx_draw_mouse_cursor(x, y);
        return;
    }
    if (!cursor_surf) return;

    int cur_type = gfx_get_cursor_type();
    if (cur_type != cursor_type_drawn) {
        gfx_render_cursor_to_buffer(cursor_surf->pixels,
                                     COMP_CURSOR_W, COMP_CURSOR_H);
        cursor_type_drawn = cur_type;
        comp_surface_damage_all(cursor_surf);
    }

    int hx, hy;
    gfx_get_cursor_hotspot(&hx, &hy);
    comp_surface_move(cursor_surf, x - hx, y - hy);
}
