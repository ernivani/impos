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
        gfx_surf_draw_string(&gs, tx + i * char_w, ty, tmp, fg, bg);
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
    /* Globe: circle outline */
    int cx = x + size/2, cy = y + size/2, r = size/2 - 1;
    (void)cx; (void)cy; (void)r;
    /* draw 2 concentric rings approximated */
    int th = SW(1);
    for (int row = y+1; row < y+size-1; row++) {
        for (int col = x+1; col < x+size-1; col++) {
            int dx = col - (x+size/2);
            int dy = row - (y+size/2);
            int d2 = dx*dx + dy*dy;
            int ro = size/2 - 1, ri = ro - th;
            if (d2 <= ro*ro && d2 >= ri*ri)
                dst[row*pitch + col] = fg;
        }
    }
    /* meridian line */
    draw_rect_pix(dst, pitch, x+size/2, y+1, th, size-2, fg);
    draw_rect_pix(dst, pitch, x+1, y+size/2, size-2, th, fg);
}

static void draw_icon_settings(uint32_t *dst, int pitch,
                                int x, int y, int size, uint32_t fg) {
    /* Gear: circle with radial notches */
    int cx = x + size/2, cy = y + size/2;
    int ro = size/2 - 1, ri = ro - SW(3), rhole = ri - SW(2);
    for (int row = y; row < y+size; row++) {
        for (int col = x; col < x+size; col++) {
            int dx = col - cx, dy = row - cy;
            int d2 = dx*dx + dy*dy;
            /* Ring */
            if (d2 >= ri*ri && d2 <= ro*ro)
                dst[row*pitch + col] = fg;
            /* Center hole (inner gear) */
            if (d2 <= rhole*rhole)
                dst[row*pitch + col] = fg;
        }
    }
    /* 8 teeth (rectangles sticking out) */
    int tooth = SW(2);
    /* top/bottom */
    draw_rect_pix(dst, pitch, cx - tooth/2, y,   tooth, SW(2)+1, fg);
    draw_rect_pix(dst, pitch, cx - tooth/2, y+size-SW(2)-1, tooth, SW(2)+1, fg);
    /* left/right */
    draw_rect_pix(dst, pitch, x, cy - tooth/2, SW(2)+1, tooth, fg);
    draw_rect_pix(dst, pitch, x+size-SW(2)-1, cy - tooth/2, SW(2)+1, tooth, fg);
}

static void draw_icon_music(uint32_t *dst, int pitch,
                             int x, int y, int size, uint32_t fg) {
    /* Quarter note: stem + filled oval head */
    int stem_x = x + SC(10), stem_y = y + SC(2);
    draw_rect_pix(dst, pitch, stem_x, stem_y, SW(2), SC(9), fg);
    /* arc at top of stem */
    draw_rect_pix(dst, pitch, x+SC(4), y+SC(2), SW(6), SW(2), fg);
    draw_rect_pix(dst, pitch, x+SC(8), y+SC(4), SW(4), SW(2), fg);
    /* oval note head */
    fill_circle(dst, pitch, x+SC(7), y+SC(11), SC(3), fg);
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
    default:            draw_icon_generic (dst, pitch, x, y, size, fg); break;
    }
}
