/* ui_fx.c — UIKit visual effects
 *
 * RGB blur, drop shadows, backdrop blur.
 * All integer arithmetic; no floating point.
 */

#include <kernel/ui_fx.h>
#include <kernel/gfx.h>
#include <string.h>
#include <stdlib.h>

/* ── RGB box blur ─────────────────────────────────────────────── */

/* Horizontal pass: blur R, G, B along each row using a sliding window sum.
 * Alpha channel is left untouched. */
static void blur_h(uint32_t *buf, int w, int h, int radius)
{
    if (w <= 1) return;
    int diam = radius * 2 + 1;

    uint32_t *tmp = (uint32_t *)malloc((size_t)w * 4);
    if (!tmp) return;

    for (int y = 0; y < h; y++) {
        uint32_t *row = buf + y * w;
        int sr = 0, sg = 0, sb = 0;

        /* Initialise window: pixels [-radius .. +radius], clamped to [0, w-1] */
        for (int i = -radius; i <= radius; i++) {
            int xi = i < 0 ? 0 : (i >= w ? w - 1 : i);
            uint32_t p = row[xi];
            sr += (p >> 16) & 0xFF;
            sg += (p >>  8) & 0xFF;
            sb +=  p        & 0xFF;
        }
        tmp[0] = (row[0] & 0xFF000000u)
               | ((uint32_t)(sr / diam) << 16)
               | ((uint32_t)(sg / diam) <<  8)
               |  (uint32_t)(sb / diam);

        for (int x = 1; x < w; x++) {
            int add = x + radius;     if (add >= w) add = w - 1;
            int rem = x - radius - 1; if (rem < 0)  rem = 0;
            uint32_t pa = row[add], pr = row[rem];
            sr += ((pa >> 16) & 0xFF) - ((pr >> 16) & 0xFF);
            sg += ((pa >>  8) & 0xFF) - ((pr >>  8) & 0xFF);
            sb += ( pa        & 0xFF) - ( pr        & 0xFF);
            tmp[x] = (row[x] & 0xFF000000u)
                   | ((uint32_t)(sr / diam) << 16)
                   | ((uint32_t)(sg / diam) <<  8)
                   |  (uint32_t)(sb / diam);
        }

        memcpy(row, tmp, (size_t)w * 4);
    }
    free(tmp);
}

/* Vertical pass: blur R, G, B along each column. */
static void blur_v(uint32_t *buf, int w, int h, int radius)
{
    if (h <= 1) return;
    int diam = radius * 2 + 1;

    uint8_t *tmp = (uint8_t *)malloc((size_t)h * 4);  /* packed rgb per column */
    if (!tmp) return;

    for (int x = 0; x < w; x++) {
        int sr = 0, sg = 0, sb = 0;

        for (int i = -radius; i <= radius; i++) {
            int yi = i < 0 ? 0 : (i >= h ? h - 1 : i);
            uint32_t p = buf[yi * w + x];
            sr += (p >> 16) & 0xFF;
            sg += (p >>  8) & 0xFF;
            sb +=  p        & 0xFF;
        }
        tmp[0] = (uint8_t)(sr / diam);
        tmp[1] = (uint8_t)(sg / diam);
        tmp[2] = (uint8_t)(sb / diam);

        for (int y = 1; y < h; y++) {
            int add = y + radius;     if (add >= h) add = h - 1;
            int rem = y - radius - 1; if (rem < 0)  rem = 0;
            uint32_t pa = buf[add * w + x];
            uint32_t pr = buf[rem * w + x];
            sr += ((pa >> 16) & 0xFF) - ((pr >> 16) & 0xFF);
            sg += ((pa >>  8) & 0xFF) - ((pr >>  8) & 0xFF);
            sb += ( pa        & 0xFF) - ( pr        & 0xFF);
            tmp[y * 3 + 0] = (uint8_t)(sr / diam);
            tmp[y * 3 + 1] = (uint8_t)(sg / diam);
            tmp[y * 3 + 2] = (uint8_t)(sb / diam);
        }

        /* Write back, preserving alpha */
        for (int y = 0; y < h; y++) {
            uint32_t old = buf[y * w + x];
            buf[y * w + x] = (old & 0xFF000000u)
                           | ((uint32_t)tmp[y * 3 + 0] << 16)
                           | ((uint32_t)tmp[y * 3 + 1] <<  8)
                           |  (uint32_t)tmp[y * 3 + 2];
        }
    }
    free(tmp);
}

void ui_fx_blur_rgb(uint32_t *buf, int w, int h, int radius)
{
    if (!buf || w <= 0 || h <= 0 || radius < 1) return;
    if (radius > 64) radius = 64;
    blur_h(buf, w, h, radius);
    blur_v(buf, w, h, radius);
}

void ui_fx_blur_rgb_3pass(uint32_t *buf, int w, int h, int radius)
{
    ui_fx_blur_rgb(buf, w, h, radius);
    ui_fx_blur_rgb(buf, w, h, radius);
    ui_fx_blur_rgb(buf, w, h, radius);
}

/* ── Drop shadow helper ──────────────────────────────────────── */

/* Corner-distance check: is pixel (px, py) inside a rounded rect? */
static int in_rounded_rect(int px, int py, int x, int y,
                            int w, int h, int r)
{
    /* Fast: not near any corner */
    if (px >= x + r && px < x + w - r) return 1;
    if (py >= y + r && py < y + h - r) return 1;

    /* Corner quadrants */
    int cx = -1, cy_c = -1;
    if      (px < x + r)     cx = x + r;
    else if (px >= x + w - r) cx = x + w - r - 1;
    if      (py < y + r)     cy_c = y + r;
    else if (py >= y + h - r) cy_c = y + h - r - 1;

    if (cx < 0 || cy_c < 0) return 1;  /* edge, not corner */

    int dx = px - cx, dy = py - cy_c;
    return (dx * dx + dy * dy) < (r * r);
}

/* Shadow parameters per level */
static const struct { int dy, blur, alpha; } shadow_cfg[4] = {
    { 0,  0,   0 },   /* NONE */
    { 3,  5,  90 },   /* SM   */
    { 6, 10, 115 },   /* MD   */
    {12, 20, 140 },   /* LG   */
};

void ui_fx_draw_shadow(gfx_surface_t *surf,
                       int x, int y, int w, int h,
                       int corner_r, int shadow_level)
{
    if (shadow_level < 1 || shadow_level > 3) return;
    if (!surf || w <= 0 || h <= 0) return;

    int dy      = shadow_cfg[shadow_level].dy;
    int blur    = shadow_cfg[shadow_level].blur;
    int opacity = shadow_cfg[shadow_level].alpha;

    /* Shadow rect: same size as the view, offset down */
    int sx = x;
    int sy = y + dy;

    /* Margin: extra pixels around the shadow for the blur to spread into.
       We render the shadow into a larger temp buffer then composite it. */
    int margin = blur + 2;
    int bw = w + margin * 2;
    int bh = h + margin * 2;

    /* Temp buffer: ARGB, shadow drawn as white in alpha channel */
    uint32_t *tmp = (uint32_t *)calloc((size_t)bw * (size_t)bh, 4);
    if (!tmp) return;

    /* Paint filled rounded rect as white (alpha = 200) into the temp buffer */
    int rr = corner_r > 0 ? corner_r : 0;
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            if (in_rounded_rect(col, row, 0, 0, w, h, rr)) {
                int brow = row + margin;
                int bcol = col + margin;
                tmp[brow * bw + bcol] = 0xC8000000u;  /* alpha=200, rgb=0 */
            }
        }
    }

    /* Blur the alpha channel using the existing gfx_box_blur (alpha-only) */
    gfx_box_blur(tmp, bw, bh, blur);
    gfx_box_blur(tmp, bw, bh, blur / 2 + 1);

    /* Composite the blurred shadow onto surf at (sx-margin, sy-margin) */
    int dst_x0 = sx - margin;
    int dst_y0 = sy - margin;
    uint32_t sa = (uint32_t)opacity;

    for (int row = 0; row < bh; row++) {
        int dy2 = dst_y0 + row;
        if (dy2 < 0 || dy2 >= surf->h) continue;

        for (int col = 0; col < bw; col++) {
            int dx2 = dst_x0 + col;
            if (dx2 < 0 || dx2 >= surf->w) continue;

            uint32_t shadow_px = tmp[row * bw + col];
            uint8_t  a_mask    = (uint8_t)((shadow_px >> 24) & 0xFF);
            if (a_mask == 0) continue;

            /* Shadow is black at (a_mask/255 × global opacity) */
            uint32_t eff_a = (uint32_t)a_mask * sa / 255;
            uint32_t inv_a = 255 - eff_a;

            uint32_t *dst = surf->buf + dy2 * surf->pitch + dx2;
            uint32_t  dp  = *dst;
            uint32_t  dr  = (dp >> 16) & 0xFF;
            uint32_t  dg  = (dp >>  8) & 0xFF;
            uint32_t  db  =  dp        & 0xFF;
            /* Blend black (0,0,0) over dst at eff_a */
            *dst = ((dr * inv_a / 255) << 16)
                 | ((dg * inv_a / 255) <<  8)
                 |  (db * inv_a / 255);
        }
    }

    free(tmp);
}

/* ── Backdrop blur ────────────────────────────────────────────── */

void ui_fx_backdrop_blur(gfx_surface_t *surf,
                         int dst_x, int dst_y, int dst_w, int dst_h,
                         int screen_x, int screen_y,
                         int corner_r, int blur_r)
{
    if (!surf || dst_w <= 0 || dst_h <= 0) return;

    /* Get the compositor's backbuffer — this holds the previous frame's
       fully composited output, i.e. everything "behind" this surface. */
    uint32_t *bb      = gfx_backbuffer();
    if (!bb) return;

    int fb_w = (int)gfx_width();
    int fb_h = (int)gfx_height();

    /* Screen coordinates of the region to sample */
    int bx = screen_x + dst_x;
    int by = screen_y + dst_y;

    /* Allocate temp buffer for the sampled + blurred region */
    uint32_t *tmp = (uint32_t *)malloc((size_t)dst_w * (size_t)dst_h * 4);
    if (!tmp) return;

    /* Copy from backbuffer, clamping out-of-bounds pixels to black */
    int pitch_px = fb_w;  /* backbuffer stride = fb_width (our hardware always has pitch=w*4) */
    for (int row = 0; row < dst_h; row++) {
        int fy = by + row;
        for (int col = 0; col < dst_w; col++) {
            int fx = bx + col;
            if (fx >= 0 && fx < fb_w && fy >= 0 && fy < fb_h)
                tmp[row * dst_w + col] = bb[fy * pitch_px + fx] & 0x00FFFFFFu;
            else
                tmp[row * dst_w + col] = 0;
        }
    }

    /* Blur: 3 passes for Gaussian approximation */
    if (blur_r < 1) blur_r = 1;
    ui_fx_blur_rgb_3pass(tmp, dst_w, dst_h, blur_r);

    /* Tint: overlay a slight dark scrim (rgba(10,16,28,0.5)) for depth */
    uint32_t scrim_r = 10, scrim_g = 16, scrim_b = 28;
    uint32_t scrim_a = 128;  /* 50% */
    uint32_t inv_scrim = 255 - scrim_a;
    for (int i = 0; i < dst_w * dst_h; i++) {
        uint32_t p  = tmp[i];
        uint32_t r  = (p >> 16) & 0xFF;
        uint32_t g  = (p >>  8) & 0xFF;
        uint32_t b  =  p        & 0xFF;
        r = (r * inv_scrim + scrim_r * scrim_a) / 255;
        g = (g * inv_scrim + scrim_g * scrim_a) / 255;
        b = (b * inv_scrim + scrim_b * scrim_a) / 255;
        tmp[i] = (r << 16) | (g << 8) | b;
    }

    /* Write to surf with rounded-corner mask */
    for (int row = 0; row < dst_h; row++) {
        int sy2 = dst_y + row;
        if (sy2 < 0 || sy2 >= surf->h) continue;
        uint32_t *dst_row = surf->buf + sy2 * surf->pitch;

        for (int col = 0; col < dst_w; col++) {
            int sx2 = dst_x + col;
            if (sx2 < 0 || sx2 >= surf->w) continue;

            /* Rounded corner clip */
            if (!in_rounded_rect(col, row, 0, 0, dst_w, dst_h, corner_r))
                continue;

            dst_row[sx2] = tmp[row * dst_w + col];
        }
    }

    free(tmp);
}
