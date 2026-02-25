/* icon_cache.c — Icon rendering: colored rounded-rects with letter avatars
 * and simple pixel-art symbolic icons drawn with rectangles/circles.
 *
 * Each icon is drawn on-the-fly (no bitmap cache needed at this size).
 */
#include <kernel/icon_cache.h>
#include <kernel/gfx.h>
#include <string.h>

void icon_cache_init(void) { /* nothing to pre-compute */ }

/* ── Drawing helpers ────────────────────────────────────────────── */

static void fill_circle(uint32_t *dst, int pitch,
                         int cx, int cy, int r, uint32_t color) {
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx*dx + dy*dy <= r*r)
                dst[(cy+dy)*pitch + (cx+dx)] = color;
}

static void draw_rect_pix(uint32_t *dst, int pitch,
                           int x, int y, int w, int h, uint32_t color) {
    for (int row = y; row < y + h; row++)
        for (int col = x; col < x + w; col++)
            dst[row * pitch + col] = color;
}

/* Draw a rounded rect filled (like CSS border-radius) */
static void draw_rrect(uint32_t *dst, int pitch,
                        int x, int y, int w, int h, int r, uint32_t color) {
    for (int row = y; row < y + h; row++) {
        for (int col = x; col < x + w; col++) {
            int dx = 0, dy = 0, inside = 1;
            if (col < x + r && row < y + r) {
                dx = col - (x + r); dy = row - (y + r);
            } else if (col >= x + w - r && row < y + r) {
                dx = col - (x + w - r - 1); dy = row - (y + r);
            } else if (col < x + r && row >= y + h - r) {
                dx = col - (x + r); dy = row - (y + h - r - 1);
            } else if (col >= x + w - r && row >= y + h - r) {
                dx = col - (x + w - r - 1); dy = row - (y + h - r - 1);
            }
            if (dx != 0 || dy != 0)
                inside = (dx*dx + dy*dy <= r*r);
            if (inside) dst[row * pitch + col] = color;
        }
    }
}

/* ── Letter avatar ──────────────────────────────────────────────── */

void icon_draw_letter(uint32_t *dst, int pitch, int x, int y,
                      int size, uint32_t bg, const char *letters) {
    int r = size / 6;
    draw_rrect(dst, pitch, x, y, size, size, r, bg);

    /* Draw up to 2 letters centered using 8x16 font logic */
    int len = 0;
    while (letters[len] && len < 2) len++;
    gfx_surface_t gs;
    gs.buf = dst; gs.w = pitch; gs.h = 65535; gs.pitch = pitch;

    int char_w = 8, char_h = 16;
    int tx = x + (size - char_w * len) / 2;
    int ty = y + (size - char_h) / 2;
    uint32_t fg = 0xFFFFFFFF;
    /* Darken text if background is light */
    int br = (bg >> 16) & 0xFF, bg_ = (bg >> 8) & 0xFF, bb = bg & 0xFF;
    int lum = (br * 299 + bg_ * 587 + bb * 114) / 1000;
    if (lum > 180) fg = 0xFF111111;

    char tmp[3] = {0};
    for (int i = 0; i < len; i++) {
        tmp[0] = letters[i]; tmp[1] = 0;
        gfx_surf_draw_string_smooth(&gs, tx + i * char_w, ty, tmp, fg, 1);
    }
}

/* ── Symbolic pixel-art icons ───────────────────────────────────── */
/* Each icon is drawn in 'size' × 'size' space.
   We use a normalized 16×16 grid scaled to actual size.     */

#define SC(v) ((v) * size / 16)   /* scale coordinate */
#define SW(v) ((v) * size / 16)   /* scale width */

static void draw_icon_terminal(uint32_t *dst, int pitch,
                                int x, int y, int size, uint32_t fg) {
    /* >_ prompt */
    /* ">" chevron */
    int m = SC(2), s = SW(1);
    draw_rect_pix(dst, pitch, x+m,       y+SC(6),  SW(4), s, fg);
    draw_rect_pix(dst, pitch, x+m+SW(2), y+SC(7),  SW(4), s, fg);
    draw_rect_pix(dst, pitch, x+m,       y+SC(8),  SW(4), s, fg);
    /* "_" underline */
    draw_rect_pix(dst, pitch, x+SC(8), y+SC(10), SW(6), s, fg);
}

static void draw_icon_files(uint32_t *dst, int pitch,
                             int x, int y, int size, uint32_t fg) {
    /* Folder outline */
    draw_rect_pix(dst, pitch, x+SC(1), y+SC(5), SW(14), SW(9), fg);
    uint32_t bg = 0xFF000000 | (((fg>>16)&0xFF)*50/255) << 16;
    (void)bg;
    /* tab on top-left */
    draw_rect_pix(dst, pitch, x+SC(1), y+SC(3), SW(5), SW(2), fg);
    /* inner clear */
    draw_rect_pix(dst, pitch, x+SC(2), y+SC(6), SW(12), SW(7), 0x00000000);
}

static void draw_icon_browser(uint32_t *dst, int pitch,
                               int x, int y, int size, uint32_t fg) {
    /* Globe: circle outline + elliptical meridian + latitude lines */
    int cx = x + size/2, cy = y + size/2;
    int r = size/2 - 1;
    int th = SW(1);
    if (th < 1) th = 1;

    /* Circle outline */
    for (int row = y+1; row < y+size-1; row++) {
        for (int col = x+1; col < x+size-1; col++) {
            int dx = col - cx, dy = row - cy;
            int d2 = dx*dx + dy*dy;
            int ri = r - th;
            if (d2 <= r*r && d2 >= ri*ri)
                dst[row*pitch + col] = fg;
        }
    }

    /* Horizontal equator */
    draw_rect_pix(dst, pitch, x+1, cy, size-2, th, fg);

    /* Vertical elliptical meridian (narrower than full circle) */
    int ew = r * 40 / 100;  /* ellipse half-width at equator */
    for (int dy = -(r-2); dy <= (r-2); dy++) {
        int row = cy + dy;
        if (row < y || row >= y + size) continue;
        int r2 = (r-2) * (r-2);
        int dx_ell = ew * (r2 - dy*dy) / r2;
        if (dx_ell < 0) dx_ell = 0;
        for (int t = 0; t < th; t++) {
            int c1 = cx + dx_ell + t, c2 = cx - dx_ell - t;
            if (c1 >= x && c1 < x+size) dst[row*pitch + c1] = fg;
            if (c2 >= x && c2 < x+size) dst[row*pitch + c2] = fg;
        }
    }

    /* Latitude lines (above and below equator) */
    {
        int lat_off = r * 45 / 100;
        /* Calculate chord width at this latitude */
        int r2 = r * r;
        int lw_sq = r2 - lat_off * lat_off;
        int lw = 0;
        for (int s = r; s >= 0; s--) { if (s*s <= lw_sq) { lw = s; break; } }
        if (lw > 2) {
            draw_rect_pix(dst, pitch, cx - lw + 1, cy - lat_off, (lw-1)*2, th, fg);
            draw_rect_pix(dst, pitch, cx - lw + 1, cy + lat_off, (lw-1)*2, th, fg);
        }
    }
}

static void draw_icon_settings(uint32_t *dst, int pitch,
                                int x, int y, int size, uint32_t fg) {
    /* Gear: ring with 8 teeth + hollow center */
    int cx = x + size/2, cy = y + size/2;
    int ro = size/2 - SW(1) - 1;  /* outer edge of ring */
    int ri = ro - SW(2);           /* inner edge of ring */
    int rh_out = SW(3);            /* center hole outer radius */
    int rh_in  = SW(2);            /* center hole inner radius */
    if (rh_in < 1) rh_in = 1;

    for (int row = y; row < y+size; row++) {
        for (int col = x; col < x+size; col++) {
            int dx = col - cx, dy = row - cy;
            int d2 = dx*dx + dy*dy;
            /* Main ring */
            if (d2 >= ri*ri && d2 <= ro*ro)
                dst[row*pitch + col] = fg;
            /* Center ring (hollow) */
            if (d2 >= rh_in*rh_in && d2 <= rh_out*rh_out)
                dst[row*pitch + col] = fg;
        }
    }

    /* 8 teeth: cardinal + diagonal */
    int tw = SW(2), th = SW(2) + 1;
    /* N, S, E, W */
    draw_rect_pix(dst, pitch, cx - tw/2, y,               tw, th, fg);
    draw_rect_pix(dst, pitch, cx - tw/2, y+size-th,       tw, th, fg);
    draw_rect_pix(dst, pitch, x,               cy - tw/2, th, tw, fg);
    draw_rect_pix(dst, pitch, x+size-th,       cy - tw/2, th, tw, fg);
    /* NE, NW, SE, SW (diagonal teeth) */
    int d45 = (ro + 1) * 707 / 1000;  /* r * cos(45) */
    int dt = SW(2);
    draw_rect_pix(dst, pitch, cx+d45-dt/2, cy-d45-dt/2, dt, dt, fg);
    draw_rect_pix(dst, pitch, cx-d45-dt/2, cy-d45-dt/2, dt, dt, fg);
    draw_rect_pix(dst, pitch, cx+d45-dt/2, cy+d45-dt/2, dt, dt, fg);
    draw_rect_pix(dst, pitch, cx-d45-dt/2, cy+d45-dt/2, dt, dt, fg);
}

static void draw_icon_music(uint32_t *dst, int pitch,
                             int x, int y, int size, uint32_t fg) {
    /* Eighth note: stem + curved flag + oval head */
    int stem_x = x + SC(10);
    int stem_top = y + SC(3);
    int stem_bot = y + SC(10);

    /* Vertical stem */
    draw_rect_pix(dst, pitch, stem_x, stem_top, SW(1)+1, stem_bot - stem_top, fg);

    /* Curved flag from top of stem, curving down-right then back */
    draw_rect_pix(dst, pitch, stem_x + 1,         stem_top,        SW(2), SW(1), fg);
    draw_rect_pix(dst, pitch, stem_x + SW(2),     stem_top+SW(1),  SW(2), SW(1), fg);
    draw_rect_pix(dst, pitch, stem_x + SW(2)+1,   stem_top+SW(2),  SW(1), SW(1), fg);
    draw_rect_pix(dst, pitch, stem_x + SW(1)+1,   stem_top+SW(3),  SW(1), SW(1), fg);

    /* Oval note head at bottom-left of stem */
    int hx = x + SC(7), hy = y + SC(12);
    int hr = SC(2) + 1;
    /* Draw slightly wider oval (ellipse: wider horizontally) */
    for (int dy = -hr; dy <= hr; dy++) {
        for (int dx = -(hr+1); dx <= (hr+1); dx++) {
            /* Ellipse: (dx/(hr+1))^2 + (dy/hr)^2 <= 1 */
            if (dx*dx*hr*hr + dy*dy*(hr+1)*(hr+1) <= hr*hr*(hr+1)*(hr+1)) {
                int r = hy + dy, c = hx + dx;
                if (r >= y && r < y+size && c >= x && c < x+size)
                    dst[r * pitch + c] = fg;
            }
        }
    }
}

static void draw_icon_code(uint32_t *dst, int pitch,
                            int x, int y, int size, uint32_t fg) {
    /* < / > */
    int s = SW(1);
    /* left < */
    draw_rect_pix(dst, pitch, x+SC(2), y+SC(6), SW(3), s, fg);
    draw_rect_pix(dst, pitch, x+SC(3), y+SC(8), SW(3), s, fg);
    draw_rect_pix(dst, pitch, x+SC(2), y+SC(10), SW(3), s, fg);
    /* right > */
    draw_rect_pix(dst, pitch, x+SC(11), y+SC(6), SW(3), s, fg);
    draw_rect_pix(dst, pitch, x+SC(10), y+SC(8), SW(3), s, fg);
    draw_rect_pix(dst, pitch, x+SC(11), y+SC(10), SW(3), s, fg);
    /* slash */
    draw_rect_pix(dst, pitch, x+SC(8), y+SC(4), SW(2), s, fg);
    draw_rect_pix(dst, pitch, x+SC(7), y+SC(6), SW(2), s, fg);
    draw_rect_pix(dst, pitch, x+SC(6), y+SC(8), SW(2), s, fg);
    draw_rect_pix(dst, pitch, x+SC(5), y+SC(10), SW(2), s, fg);
    draw_rect_pix(dst, pitch, x+SC(4), y+SC(12), SW(2), s, fg);
}

static void draw_icon_monitor(uint32_t *dst, int pitch,
                               int x, int y, int size, uint32_t fg) {
    /* Screen + stand */
    draw_rect_pix(dst, pitch, x+SC(1), y+SC(2), SW(14), SC(9), fg);
    draw_rect_pix(dst, pitch, x+SC(2), y+SC(3), SW(12), SC(7), 0); /* clear interior */
    draw_rect_pix(dst, pitch, x+SC(7), y+SC(11), SW(2), SC(3), fg);
    draw_rect_pix(dst, pitch, x+SC(5), y+SC(13), SW(6), SW(1), fg);
}

static void draw_icon_email(uint32_t *dst, int pitch,
                             int x, int y, int size, uint32_t fg) {
    /* Envelope: rectangle body + V flap */
    int s = SW(1);
    draw_rect_pix(dst, pitch, x+SC(1), y+SC(4), SW(14), SC(9), fg);
    /* Clear interior */
    draw_rect_pix(dst, pitch, x+SC(2), y+SC(5), SW(12), SC(7), 0x00000000);
    /* V-flap (stepped diagonal from top-left and top-right to center) */
    for (int d = 0; d < 5; d++) {
        draw_rect_pix(dst, pitch, x+SC(1)+SC(d), y+SC(4)+SC(d), SW(2), s, fg);
        draw_rect_pix(dst, pitch, x+SC(13)-SC(d), y+SC(4)+SC(d), SW(2), s, fg);
    }
}

static void draw_icon_chat(uint32_t *dst, int pitch,
                            int x, int y, int size, uint32_t fg) {
    /* Speech bubble */
    draw_rrect(dst, pitch, x+SC(1), y+SC(2), SW(14), SC(9), SW(3), fg);
    /* Clear interior */
    draw_rrect(dst, pitch, x+SC(2), y+SC(3), SW(12), SC(7), SW(2), 0x00000000);
    /* Tail at bottom-left */
    draw_rect_pix(dst, pitch, x+SC(3), y+SC(11), SW(2), SC(2), fg);
    draw_rect_pix(dst, pitch, x+SC(2), y+SC(12), SW(2), SC(2), fg);
}

static void draw_icon_video(uint32_t *dst, int pitch,
                             int x, int y, int size, uint32_t fg) {
    /* Play triangle */
    int cx = x + size/2 + SC(1);
    int cy = y + size/2;
    int s = SW(1);
    for (int d = 0; d < SC(6); d++) {
        int w = d * 2 / 3 + 1;
        if (w < 1) w = 1;
        draw_rect_pix(dst, pitch, cx - SC(3) + d, cy - w, s, w * 2 + 1, fg);
    }
}

static void draw_icon_image(uint32_t *dst, int pitch,
                              int x, int y, int size, uint32_t fg) {
    /* Landscape: frame + mountain + sun */
    draw_rect_pix(dst, pitch, x+SC(1), y+SC(2), SW(14), SC(12), fg);
    draw_rect_pix(dst, pitch, x+SC(2), y+SC(3), SW(12), SC(10), 0x00000000);
    /* Sun: small circle top-right */
    fill_circle(dst, pitch, x+SC(11), y+SC(5), SC(2), fg);
    /* Mountain peaks (stepped triangle) */
    for (int d = 0; d < SC(5); d++) {
        int mw = d * 2 + 1;
        draw_rect_pix(dst, pitch, x+SC(6)-d, y+SC(12)-d-1, mw, SW(1), fg);
    }
    /* Smaller peak */
    for (int d = 0; d < SC(3); d++) {
        int mw = d * 2 + 1;
        draw_rect_pix(dst, pitch, x+SC(10)-d, y+SC(12)-d-1, mw, SW(1), fg);
    }
}

static void draw_icon_pdf(uint32_t *dst, int pitch,
                           int x, int y, int size, uint32_t fg) {
    /* Document with folded corner */
    draw_rect_pix(dst, pitch, x+SC(3), y+SC(1), SW(10), SC(14), fg);
    /* Clear interior */
    draw_rect_pix(dst, pitch, x+SC(4), y+SC(2), SW(8), SC(12), 0x00000000);
    /* Folded corner (top-right) */
    draw_rect_pix(dst, pitch, x+SC(10), y+SC(1), SW(3), SC(3), 0x00000000);
    draw_rect_pix(dst, pitch, x+SC(10), y+SC(4), SW(3), SW(1), fg);
    draw_rect_pix(dst, pitch, x+SC(10), y+SC(1), SW(1), SC(3), fg);
    /* Text lines */
    draw_rect_pix(dst, pitch, x+SC(5), y+SC(6), SW(6), SW(1), fg);
    draw_rect_pix(dst, pitch, x+SC(5), y+SC(8), SW(6), SW(1), fg);
    draw_rect_pix(dst, pitch, x+SC(5), y+SC(10), SW(4), SW(1), fg);
}

static void draw_icon_gamepad(uint32_t *dst, int pitch,
                               int x, int y, int size, uint32_t fg) {
    /* Gamepad body */
    draw_rrect(dst, pitch, x+SC(1), y+SC(4), SW(14), SC(8), SW(3), fg);
    /* Clear interior */
    draw_rrect(dst, pitch, x+SC(2), y+SC(5), SW(12), SC(6), SW(2), 0x00000000);
    /* D-pad (cross on left) */
    draw_rect_pix(dst, pitch, x+SC(4), y+SC(6), SW(1), SC(4), fg);
    draw_rect_pix(dst, pitch, x+SC(3), y+SC(7), SW(3), SC(2), fg);
    /* Buttons (dots on right) */
    fill_circle(dst, pitch, x+SC(11), y+SC(7), SW(1), fg);
    fill_circle(dst, pitch, x+SC(13), y+SC(7), SW(1), fg);
    fill_circle(dst, pitch, x+SC(12), y+SC(6), SW(1), fg);
    fill_circle(dst, pitch, x+SC(12), y+SC(8), SW(1), fg);
}

static void draw_icon_disk(uint32_t *dst, int pitch,
                            int x, int y, int size, uint32_t fg) {
    /* Hard drive: rounded rect with line across */
    draw_rrect(dst, pitch, x+SC(1), y+SC(4), SW(14), SC(9), SW(2), fg);
    draw_rrect(dst, pitch, x+SC(2), y+SC(5), SW(12), SC(7), SW(1), 0x00000000);
    /* Divider line */
    draw_rect_pix(dst, pitch, x+SC(2), y+SC(8), SW(12), SW(1), fg);
    /* LED dot */
    fill_circle(dst, pitch, x+SC(12), y+SC(10), SW(1), fg);
}

static void draw_icon_users(uint32_t *dst, int pitch,
                              int x, int y, int size, uint32_t fg) {
    /* Two people: head circles + body arcs */
    /* Person 1 (left, slightly behind) */
    fill_circle(dst, pitch, x+SC(5), y+SC(4), SC(2), fg);
    draw_rrect(dst, pitch, x+SC(2), y+SC(7), SW(6), SC(5), SW(2), fg);
    /* Person 2 (right, in front) */
    fill_circle(dst, pitch, x+SC(11), y+SC(4), SC(2), fg);
    draw_rrect(dst, pitch, x+SC(8), y+SC(7), SW(6), SC(5), SW(2), fg);
}

static void draw_icon_download(uint32_t *dst, int pitch,
                                int x, int y, int size, uint32_t fg) {
    /* Arrow pointing down + bar at bottom */
    int s = SW(1);
    /* Vertical stem */
    draw_rect_pix(dst, pitch, x+SC(7), y+SC(2), SW(2), SC(7), fg);
    /* Arrow head (V shape) */
    for (int d = 0; d < 4; d++) {
        draw_rect_pix(dst, pitch, x+SC(8)-SC(d)-SC(1), y+SC(7)+SC(d), SW(2)+SC(d)*2, s, fg);
    }
    /* Base line */
    draw_rect_pix(dst, pitch, x+SC(3), y+SC(13), SW(10), SW(1), fg);
}

static void draw_icon_table(uint32_t *dst, int pitch,
                              int x, int y, int size, uint32_t fg) {
    /* Grid: outer rectangle + inner lines */
    draw_rect_pix(dst, pitch, x+SC(1), y+SC(2), SW(14), SC(12), fg);
    draw_rect_pix(dst, pitch, x+SC(2), y+SC(3), SW(12), SC(10), 0x00000000);
    /* Horizontal dividers */
    draw_rect_pix(dst, pitch, x+SC(1), y+SC(6), SW(14), SW(1), fg);
    draw_rect_pix(dst, pitch, x+SC(1), y+SC(10), SW(14), SW(1), fg);
    /* Vertical divider */
    draw_rect_pix(dst, pitch, x+SC(6), y+SC(2), SW(1), SC(12), fg);
}

static void draw_icon_pen(uint32_t *dst, int pitch,
                           int x, int y, int size, uint32_t fg) {
    /* Diagonal pencil from top-right to bottom-left */
    int s = SW(1);
    for (int d = 0; d < 10; d++) {
        int px_ = x + SC(12) - SC(d);
        int py  = y + SC(3) + SC(d);
        draw_rect_pix(dst, pitch, px_, py, SW(2), SW(2), fg);
    }
    /* Pencil tip */
    draw_rect_pix(dst, pitch, x+SC(2), y+SC(13), SW(1), s, fg);
    /* Eraser end */
    draw_rect_pix(dst, pitch, x+SC(12), y+SC(2), SW(3), SW(3), fg);
}

static void draw_icon_calendar(uint32_t *dst, int pitch,
                                int x, int y, int size, uint32_t fg) {
    /* Calendar page */
    draw_rect_pix(dst, pitch, x+SC(2), y+SC(2), SW(12), SC(13), fg);
    /* Clear interior */
    draw_rect_pix(dst, pitch, x+SC(3), y+SC(6), SW(10), SC(8), 0x00000000);
    /* Header bar */
    draw_rect_pix(dst, pitch, x+SC(2), y+SC(2), SW(12), SC(4), fg);
    /* Hanging clips */
    draw_rect_pix(dst, pitch, x+SC(5), y+SC(1), SW(1), SC(3), fg);
    draw_rect_pix(dst, pitch, x+SC(10), y+SC(1), SW(1), SC(3), fg);
    /* Date dots (2x3 grid) */
    draw_rect_pix(dst, pitch, x+SC(4), y+SC(8), SW(2), SW(1), fg);
    draw_rect_pix(dst, pitch, x+SC(7), y+SC(8), SW(2), SW(1), fg);
    draw_rect_pix(dst, pitch, x+SC(10), y+SC(8), SW(2), SW(1), fg);
    draw_rect_pix(dst, pitch, x+SC(4), y+SC(10), SW(2), SW(1), fg);
    draw_rect_pix(dst, pitch, x+SC(7), y+SC(10), SW(2), SW(1), fg);
    draw_rect_pix(dst, pitch, x+SC(10), y+SC(10), SW(2), SW(1), fg);
    draw_rect_pix(dst, pitch, x+SC(4), y+SC(12), SW(2), SW(1), fg);
    draw_rect_pix(dst, pitch, x+SC(7), y+SC(12), SW(2), SW(1), fg);
}

static void draw_icon_radio(uint32_t *dst, int pitch,
                              int x, int y, int size, uint32_t fg) {
    /* Antenna with signal arcs */
    /* Antenna stick */
    draw_rect_pix(dst, pitch, x+SC(8), y+SC(6), SW(1), SC(8), fg);
    /* Base */
    draw_rect_pix(dst, pitch, x+SC(5), y+SC(13), SW(6), SW(1), fg);
    /* Signal arcs (quarter-circle arcs radiating from top) */
    int acx = x + SC(8), acy = y + SC(6);
    for (int arc = 0; arc < 3; arc++) {
        int r = SC(2) + SC(2) * arc;
        for (int dy = -r; dy <= 0; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                int d2 = dx*dx + dy*dy;
                if (d2 >= (r-1)*(r-1) && d2 <= r*r) {
                    int col_ = acx + dx, row_ = acy + dy;
                    if (col_ >= x && col_ < x+size && row_ >= y && row_ < y+size)
                        dst[row_ * pitch + col_] = fg;
                }
            }
        }
    }
}

static void draw_icon_generic(uint32_t *dst, int pitch,
                               int x, int y, int size, uint32_t fg) {
    /* Simple rounded rect outline */
    int r = SW(2);
    draw_rrect(dst, pitch, x+SC(2), y+SC(2), SW(12), SC(12), r, fg);
    /* clear inside */
    draw_rect_pix(dst, pitch, x+SC(3), y+SC(3), SW(10), SC(10), 0);
}

#undef SC
#undef SW

/* ── Public API ─────────────────────────────────────────────────── */

void icon_draw(int icon_id, uint32_t *dst, int pitch,
               int x, int y, int size, uint32_t bg, uint32_t fg) {
    int r = size / 6;
    if (r < 2) r = 2;

    /* Draw background rounded rect */
    draw_rrect(dst, pitch, x, y, size, size, r, bg);

    /* Draw symbol */
    switch (icon_id) {
    case ICON_TERMINAL: draw_icon_terminal(dst, pitch, x, y, size, fg); break;
    case ICON_FILES:    draw_icon_files   (dst, pitch, x, y, size, fg); break;
    case ICON_BROWSER:  draw_icon_browser (dst, pitch, x, y, size, fg); break;
    case ICON_SETTINGS: draw_icon_settings(dst, pitch, x, y, size, fg); break;
    case ICON_MUSIC:    draw_icon_music   (dst, pitch, x, y, size, fg); break;
    case ICON_CODE:     draw_icon_code    (dst, pitch, x, y, size, fg); break;
    case ICON_MONITOR:  draw_icon_monitor (dst, pitch, x, y, size, fg); break;
    case ICON_EMAIL:    draw_icon_email   (dst, pitch, x, y, size, fg); break;
    case ICON_CHAT:     draw_icon_chat    (dst, pitch, x, y, size, fg); break;
    case ICON_VIDEO:    draw_icon_video   (dst, pitch, x, y, size, fg); break;
    case ICON_IMAGE:    draw_icon_image   (dst, pitch, x, y, size, fg); break;
    case ICON_PDF:      draw_icon_pdf     (dst, pitch, x, y, size, fg); break;
    case ICON_GAMEPAD:  draw_icon_gamepad (dst, pitch, x, y, size, fg); break;
    case ICON_DISK:     draw_icon_disk    (dst, pitch, x, y, size, fg); break;
    case ICON_USERS:    draw_icon_users   (dst, pitch, x, y, size, fg); break;
    case ICON_DOWNLOAD: draw_icon_download(dst, pitch, x, y, size, fg); break;
    case ICON_TABLE:    draw_icon_table   (dst, pitch, x, y, size, fg); break;
    case ICON_PEN:      draw_icon_pen     (dst, pitch, x, y, size, fg); break;
    case ICON_CALENDAR: draw_icon_calendar(dst, pitch, x, y, size, fg); break;
    case ICON_RADIO:    draw_icon_radio   (dst, pitch, x, y, size, fg); break;
    default:            draw_icon_generic (dst, pitch, x, y, size, fg); break;
    }
}
