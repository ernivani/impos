/* ui_font.c — UIKit unified font API
 *
 * Active tier is selected at init time and never changes mid-session.
 * Vec tier is always ready; TTF tier requires a call to ui_font_load_ttf().
 */

#include <kernel/ui_font.h>
#include <kernel/gfx.h>
#include <kernel/gfx_ttf.h>
#include <string.h>
#include <stdlib.h>

/* ── State ───────────────────────────────────────────────────── */

static int          font_ready  = 0;
static int          use_ttf     = 0;
static ttf_font_t   ttf_font;

/* ── Init ────────────────────────────────────────────────────── */

void ui_font_init(void)
{
    if (font_ready) return;
    gfx_builtin_font_init();  /* build vec rectangles from font8x16 */
    font_ready = 1;
}

int ui_font_load_ttf(const uint8_t *data, uint32_t len)
{
    if (!font_ready) ui_font_init();
    if (ttf_load(&ttf_font, data, len) == 0) {
        use_ttf = 1;
        return 1;
    }
    return 0;
}

/* ── Metrics ─────────────────────────────────────────────────── */

int ui_font_width(const char *str, int px)
{
    if (!str || !*str) return 0;
    if (use_ttf)
        return gfx_string_vec_width(str, px);  /* vec matches TTF closely */
    return gfx_string_vec_width(str, px);
}

int ui_font_height(int px)
{
    /* The vec font renders in a box sized px × px.
       Add 20% leading for readability.               */
    return px + px / 5;
}

/* ── Drawing ─────────────────────────────────────────────────── */

void ui_font_draw(gfx_surface_t *surf, int x, int y,
                  const char *str, uint32_t color, int px)
{
    if (!font_ready || !str || !*str) return;
    if (px < 1) px = 13;

    if (use_ttf) {
        gfx_surf_draw_string_ttf(surf, x, y, str, color, &ttf_font, px);
    } else {
        gfx_surf_draw_string_vec(surf, x, y, str, color, px);
    }
}

void ui_font_draw_in_rect(gfx_surface_t *surf,
                          int rx, int ry, int rw, int rh,
                          const char *str, uint32_t color,
                          int px, int align)
{
    if (!font_ready || !str || !*str) return;
    if (px < 1) px = 13;

    int tw = ui_font_width(str, px);
    int th = ui_font_height(px);

    int tx, ty;

    /* Horizontal alignment */
    switch (align) {
    case UI_FONT_CENTER:
        tx = rx + (rw - tw) / 2;
        break;
    case UI_FONT_RIGHT:
        tx = rx + rw - tw;
        break;
    default: /* UI_FONT_LEFT */
        tx = rx;
        break;
    }

    /* Always vertically centred */
    ty = ry + (rh - th) / 2;

    /* Clamp to rect */
    if (tx < rx) tx = rx;

    ui_font_draw(surf, tx, ty, str, color, px);
}

void ui_font_ellipsis(const char *str, int px, int max_w,
                      char *buf, int buf_len)
{
    if (!str || buf_len < 4) return;

    int full_w = ui_font_width(str, px);
    if (full_w <= max_w) {
        /* Fits without truncation */
        int slen = (int)strlen(str);
        if (slen >= buf_len) slen = buf_len - 1;
        memcpy(buf, str, (size_t)slen);
        buf[slen] = '\0';
        return;
    }

    /* Binary search for the longest prefix that fits with "..." */
    int ellipsis_w = ui_font_width("...", px);
    int avail = max_w - ellipsis_w;
    if (avail <= 0) {
        buf[0] = '\0';
        return;
    }

    int lo = 0, hi = (int)strlen(str);
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        /* Measure prefix of length mid */
        char tmp[256];
        if (mid >= 255) mid = 255;
        memcpy(tmp, str, (size_t)mid);
        tmp[mid] = '\0';
        if (ui_font_width(tmp, px) <= avail)
            lo = mid;
        else
            hi = mid - 1;
    }

    int out = lo < buf_len - 4 ? lo : buf_len - 4;
    memcpy(buf, str, (size_t)out);
    buf[out]   = '.';
    buf[out+1] = '.';
    buf[out+2] = '.';
    buf[out+3] = '\0';
}
