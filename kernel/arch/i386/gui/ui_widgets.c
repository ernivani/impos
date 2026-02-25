/* ui_widgets.c — UIKit widget library
 *
 * Modifier chain API + pre-styled widget constructors.
 * All state is in static pools; no per-widget malloc.
 */

#include <kernel/ui_widgets.h>
#include <kernel/ui_view.h>
#include <kernel/ui_layout.h>
#include <kernel/ui_font.h>
#include <kernel/ui_fx.h>
#include <kernel/ui_token.h>
#include <kernel/gfx.h>
#include <string.h>

/* ── Private colour helpers ──────────────────────────────────────
 * Pre-mixed values for common transparent-over-dark composites.
 * (The render pass has no per-primitive alpha on borders yet.)        */

/* 8% white over TOK_BG_ELEVATED (20,28,44) ≈ (39,46,62) */
#define _COL_BORDER_SUBTLE   GFX_RGB( 39,  46,  62)
/* 10% white over TOK_BG_SURFACE (12,20,40) ≈ (36,42,62) */
#define _COL_BORDER_WINDOW   GFX_RGB( 36,  42,  62)
/* 6% white over TOK_BG_SURFACE for hover lift */
#define _COL_HOVER_LIFT      GFX_RGB( 27,  35,  55)
/* Pressed: 12% black over TOK_BG_ELEVATED */
#define _COL_ACTIVE_DARK     GFX_RGB( 16,  23,  38)

/* ═══════════════════════════════════════════════════════════════
 * Modifier API
 * ═══════════════════════════════════════════════════════════════ */

/* ── Base style ─────────────────────────────────────────────── */

ui_view_t *ui_set_bg(ui_view_t *v, uint32_t c)
{
    if (v) v->style.bg = c;
    return v;
}

ui_view_t *ui_set_fg(ui_view_t *v, uint32_t c)
{
    if (v) v->style.fg = c;
    return v;
}

ui_view_t *ui_set_radius(ui_view_t *v, int r)
{
    if (v) v->style.radius = (uint8_t)(r > 255 ? 255 : r);
    return v;
}

ui_view_t *ui_set_shadow(ui_view_t *v, int level)
{
    if (v) v->style.shadow = (uint8_t)(level > 3 ? 3 : level < 0 ? 0 : level);
    return v;
}

ui_view_t *ui_set_opacity(ui_view_t *v, int alpha)
{
    if (v) v->style.opacity = (uint8_t)(alpha > 255 ? 255 : alpha < 0 ? 0 : alpha);
    return v;
}

ui_view_t *ui_set_border(ui_view_t *v, uint32_t c, int w)
{
    if (v) {
        v->style.border_color = c;
        v->style.border_w     = (uint8_t)(w > 255 ? 255 : w);
    }
    return v;
}

ui_view_t *ui_set_font(ui_view_t *v, int px)
{
    if (v) v->style.font_px = (uint8_t)(px > 255 ? 255 : px < 1 ? 1 : px);
    return v;
}

ui_view_t *ui_set_text_align(ui_view_t *v, int align)
{
    if (v) v->style.text_align = (uint8_t)align;
    return v;
}

/* ── Pseudo-state overrides ─────────────────────────────────── */

ui_view_t *ui_set_hover_bg(ui_view_t *v, uint32_t c)
{
    if (v) { v->style_hover.bg = c; v->style_hover.opacity = 255; }
    return v;
}

ui_view_t *ui_set_hover_fg(ui_view_t *v, uint32_t c)
{
    if (v) { v->style_hover.fg = c; v->style_hover.opacity = 255; }
    return v;
}

ui_view_t *ui_set_active_bg(ui_view_t *v, uint32_t c)
{
    if (v) { v->style_active.bg = c; v->style_active.opacity = 255; }
    return v;
}

ui_view_t *ui_set_focus_border(ui_view_t *v, uint32_t c)
{
    if (v) {
        v->style_focus.border_color = c;
        v->style_focus.border_w     = 2;
        v->style_focus.opacity      = 255;
    }
    return v;
}

ui_view_t *ui_set_focus_bg(ui_view_t *v, uint32_t c)
{
    if (v) { v->style_focus.bg = c; v->style_focus.opacity = 255; }
    return v;
}

/* ── Size ───────────────────────────────────────────────────── */

ui_view_t *ui_set_w(ui_view_t *v, int px)
{
    if (v) { v->size.w_mode = UI_SIZE_FIXED; v->size.w = (int16_t)px; }
    return v;
}

ui_view_t *ui_set_h(ui_view_t *v, int px)
{
    if (v) { v->size.h_mode = UI_SIZE_FIXED; v->size.h = (int16_t)px; }
    return v;
}

ui_view_t *ui_set_wh(ui_view_t *v, int w, int h)
{
    if (v) {
        v->size.w_mode = UI_SIZE_FIXED; v->size.w = (int16_t)w;
        v->size.h_mode = UI_SIZE_FIXED; v->size.h = (int16_t)h;
    }
    return v;
}

ui_view_t *ui_set_fill(ui_view_t *v)
{
    if (v) {
        v->size.w_mode = UI_SIZE_FILL; v->size.flex = 1000;
        v->size.h_mode = UI_SIZE_FILL;
    }
    return v;
}

ui_view_t *ui_set_fill_w(ui_view_t *v)
{
    if (v) { v->size.w_mode = UI_SIZE_FILL; v->size.flex = 1000; }
    return v;
}

ui_view_t *ui_set_fill_h(ui_view_t *v)
{
    if (v) v->size.h_mode = UI_SIZE_FILL;
    return v;
}

ui_view_t *ui_set_hug(ui_view_t *v)
{
    if (v) { v->size.w_mode = UI_SIZE_HUG; v->size.h_mode = UI_SIZE_HUG; }
    return v;
}

/* ── Layout ─────────────────────────────────────────────────── */

ui_view_t *ui_set_pad(ui_view_t *v, int px)
{
    if (v) {
        v->layout.pad_top    = (int16_t)px;
        v->layout.pad_right  = (int16_t)px;
        v->layout.pad_bottom = (int16_t)px;
        v->layout.pad_left   = (int16_t)px;
    }
    return v;
}

ui_view_t *ui_set_pad4(ui_view_t *v, int t, int r, int b, int l)
{
    if (v) {
        v->layout.pad_top    = (int16_t)t;
        v->layout.pad_right  = (int16_t)r;
        v->layout.pad_bottom = (int16_t)b;
        v->layout.pad_left   = (int16_t)l;
    }
    return v;
}

ui_view_t *ui_set_gap(ui_view_t *v, int px)
{
    if (v) v->layout.gap = (int16_t)px;
    return v;
}

ui_view_t *ui_set_align(ui_view_t *v, int a)
{
    if (v) v->layout.align = (uint8_t)a;
    return v;
}

ui_view_t *ui_set_justify(ui_view_t *v, int j)
{
    if (v) v->layout.justify = (uint8_t)j;
    return v;
}

ui_view_t *ui_set_clip(ui_view_t *v)
{
    if (v) v->clip = 1;
    return v;
}

/* ── Identity ───────────────────────────────────────────────── */

ui_view_t *ui_set_name(ui_view_t *v, const char *name)
{
    if (v) v->debug_name = name;
    return v;
}

/* ═══════════════════════════════════════════════════════════════
 * Shared button style helper
 * ═══════════════════════════════════════════════════════════════ */

static void apply_button_base(ui_view_t *v, const char *label,
                               void (*on_click)(ui_view_t *, int, int, void *),
                               void *ctx)
{
    v->text              = label;
    v->style.font_px     = 13;
    v->style.text_align  = UI_TEXT_CENTER;
    v->style.opacity     = 255;
    v->style.radius      = TOK_RADIUS_SM;

    /* Padding: 7px top/bottom, 16px left/right */
    v->layout.pad_top    = 7;
    v->layout.pad_bottom = 7;
    v->layout.pad_left   = 16;
    v->layout.pad_right  = 16;
    v->layout.align      = UI_ALIGN_CENTER;
    v->layout.justify    = UI_JUST_CENTER;

    v->size.w_mode = UI_SIZE_HUG;
    v->size.h_mode = UI_SIZE_HUG;

    v->on_click  = on_click;
    v->event_ctx = ctx;
    v->focusable = 1;
}

/* ═══════════════════════════════════════════════════════════════
 * Input widget
 * ═══════════════════════════════════════════════════════════════ */

typedef struct {
    char       *buf;
    int         buf_len;
    int         cursor;
    const char *placeholder;
    void      (*on_change)(ui_view_t *, void *);
    void       *ctx;
} input_data_t;

#define INPUT_POOL_SIZE  16
static input_data_t input_pool[INPUT_POOL_SIZE];
static uint8_t      input_used[INPUT_POOL_SIZE];

static input_data_t *input_alloc(void)
{
    for (int i = 0; i < INPUT_POOL_SIZE; i++) {
        if (!input_used[i]) {
            input_used[i] = 1;
            memset(&input_pool[i], 0, sizeof(input_pool[i]));
            return &input_pool[i];
        }
    }
    return NULL;
}

static void input_paint(ui_view_t *v, gfx_surface_t *surf)
{
    input_data_t *d = (input_data_t *)v->userdata;
    if (!d) return;

    ui_style_t s = ui_view_active_style(v);
    int px  = s.font_px ? (int)s.font_px : 13;
    int pad = 10;
    int tx  = v->ax + pad;
    int tw  = v->aw - pad * 2;
    int th  = v->ah;
    int len = (int)strlen(d->buf);

    if (len == 0 && !v->focused && d->placeholder) {
        /* Placeholder */
        ui_font_draw_in_rect(surf, tx, v->ay, tw, th,
                             d->placeholder, TOK_TEXT_DIM, px, UI_FONT_LEFT);
    } else if (len > 0) {
        ui_font_draw_in_rect(surf, tx, v->ay, tw, th,
                             d->buf, TOK_TEXT_PRIMARY, px, UI_FONT_LEFT);
    }

    /* Cursor: 2px vertical bar at cursor position */
    if (v->focused) {
        char tmp[512];
        int clen = d->cursor;
        if (clen > (int)sizeof(tmp) - 1) clen = (int)sizeof(tmp) - 1;
        memcpy(tmp, d->buf, (size_t)clen);
        tmp[clen] = '\0';

        int cx = tx + ui_font_width(tmp, px);
        int fh = ui_font_height(px);
        int cy = v->ay + (v->ah - fh) / 2;
        gfx_surf_fill_rect(surf, cx, cy, 2, fh, TOK_ACCENT);
    }
}

static void input_key(ui_view_t *v, int key, void *ctx)
{
    (void)ctx;
    input_data_t *d = (input_data_t *)v->userdata;
    if (!d) return;

    int len = (int)strlen(d->buf);

    if (key == 0x08 || key == 127) {
        /* Backspace: remove char before cursor */
        if (d->cursor > 0) {
            memmove(d->buf + d->cursor - 1,
                    d->buf + d->cursor,
                    (size_t)(len - d->cursor) + 1);
            d->cursor--;
        }
    } else if (key == 0x4B) {
        /* Left arrow */
        if (d->cursor > 0) d->cursor--;
    } else if (key == 0x4D) {
        /* Right arrow */
        if (d->cursor < len) d->cursor++;
    } else if (key == 0x01) {
        /* Ctrl+A: cursor to start */
        d->cursor = 0;
    } else if (key == 0x05) {
        /* Ctrl+E: cursor to end */
        d->cursor = len;
    } else if (key >= 0x20 && key < 0x7F && len < d->buf_len - 1) {
        /* Printable char: insert at cursor */
        memmove(d->buf + d->cursor + 1,
                d->buf + d->cursor,
                (size_t)(len - d->cursor) + 1);
        d->buf[d->cursor++] = (char)key;
    }

    ui_view_mark_dirty(v);
    if (d->on_change)
        d->on_change(v, d->ctx);
}

/* ═══════════════════════════════════════════════════════════════
 * Card widget
 * ═══════════════════════════════════════════════════════════════ */

typedef struct {
    int      blur_r;
    uint32_t tint;
    uint8_t  tint_alpha;
} card_data_t;

#define CARD_POOL_SIZE  8
static card_data_t card_pool[CARD_POOL_SIZE];
static uint8_t     card_used[CARD_POOL_SIZE];

static card_data_t *card_alloc(void)
{
    for (int i = 0; i < CARD_POOL_SIZE; i++) {
        if (!card_used[i]) {
            card_used[i] = 1;
            memset(&card_pool[i], 0, sizeof(card_pool[i]));
            return &card_pool[i];
        }
    }
    return NULL;
}

static void card_paint(ui_view_t *v, gfx_surface_t *surf)
{
    card_data_t *d = (card_data_t *)v->userdata;
    if (!d || d->blur_r <= 0) return;

    /* Backdrop blur: sample the previous compositor frame at this rect */
    ui_fx_backdrop_blur(surf,
                        v->ax, v->ay, v->aw, v->ah,
                        v->ax, v->ay,
                        (int)v->style.radius, d->blur_r);

    /* Tint overlay for depth (dark scrim) */
    if (d->tint && d->tint_alpha > 0) {
        if (v->style.radius > 0)
            gfx_surf_rounded_rect_alpha(surf, v->ax, v->ay, v->aw, v->ah,
                                        (int)v->style.radius,
                                        d->tint, d->tint_alpha);
        else
            gfx_surf_fill_rect_alpha(surf, v->ax, v->ay, v->aw, v->ah,
                                     d->tint, d->tint_alpha);
    }
}

/* ═══════════════════════════════════════════════════════════════
 * Init
 * ═══════════════════════════════════════════════════════════════ */

void ui_widgets_init(void)
{
    memset(input_pool, 0, sizeof(input_pool));
    memset(input_used, 0, sizeof(input_used));
    memset(card_pool,  0, sizeof(card_pool));
    memset(card_used,  0, sizeof(card_used));
    ui_font_init();
}

/* ═══════════════════════════════════════════════════════════════
 * Widget constructors
 * ═══════════════════════════════════════════════════════════════ */

/* ── Label ──────────────────────────────────────────────────── */

ui_view_t *ui_label(const char *text, uint32_t fg, int px)
{
    ui_view_t *v = ui_view_create();
    if (!v) return NULL;

    v->text            = text;
    v->style.fg        = fg;
    v->style.font_px   = (uint8_t)(px < 1 ? 13 : px > 255 ? 255 : px);
    v->style.opacity   = 255;
    v->size.w_mode     = UI_SIZE_HUG;
    v->size.h_mode     = UI_SIZE_HUG;
    return v;
}

ui_view_t *ui_label_dim(const char *text, int px)
{
    return ui_label(text, TOK_TEXT_SECONDARY, px);
}

/* ── Ghost button ───────────────────────────────────────────── */

ui_view_t *ui_button(const char *label,
                     void (*on_click)(ui_view_t *, int, int, void *),
                     void *ctx)
{
    ui_view_t *v = ui_view_create();
    if (!v) return NULL;

    apply_button_base(v, label, on_click, ctx);

    v->style.bg           = GFX_RGB(26, 34, 52);
    v->style.fg           = TOK_TEXT_PRIMARY;
    v->style.border_color = _COL_BORDER_SUBTLE;
    v->style.border_w     = 1;

    /* Hover: lifted bg */
    v->style_hover.bg      = _COL_HOVER_LIFT;
    v->style_hover.opacity = 255;

    /* Active: pressed darker */
    v->style_active.bg      = _COL_ACTIVE_DARK;
    v->style_active.opacity = 255;

    /* Focus: accent border ring */
    v->style_focus.border_color = TOK_BORDER_FOCUS;
    v->style_focus.border_w     = 2;
    v->style_focus.opacity      = 255;

    return v;
}

/* ── Primary button (accent fill) ───────────────────────────── */

ui_view_t *ui_button_primary(const char *label,
                              void (*on_click)(ui_view_t *, int, int, void *),
                              void *ctx)
{
    ui_view_t *v = ui_view_create();
    if (!v) return NULL;

    apply_button_base(v, label, on_click, ctx);

    v->style.bg      = TOK_ACCENT;
    v->style.fg      = TOK_TEXT_ON_ACCENT;
    v->style.shadow  = TOK_SHADOW_SM;

    v->style_hover.bg      = TOK_ACCENT_HOVER;
    v->style_hover.opacity = 255;

    v->style_active.bg      = TOK_ACCENT_PRESS;
    v->style_active.opacity = 255;

    v->style_focus.border_color = TOK_TEXT_ON_ACCENT;
    v->style_focus.border_w     = 2;
    v->style_focus.opacity      = 255;

    return v;
}

/* ── Destructive button (red fill) ──────────────────────────── */

ui_view_t *ui_button_danger(const char *label,
                             void (*on_click)(ui_view_t *, int, int, void *),
                             void *ctx)
{
    ui_view_t *v = ui_view_create();
    if (!v) return NULL;

    apply_button_base(v, label, on_click, ctx);

    v->style.bg      = TOK_BTN_CLOSE;   /* #FF5F57 */
    v->style.fg      = TOK_TEXT_ON_ACCENT;

    v->style_hover.bg      = GFX_RGB(255, 100, 90);
    v->style_hover.opacity = 255;

    v->style_active.bg      = GFX_RGB(200, 50, 40);
    v->style_active.opacity = 255;

    v->style_focus.border_color = TOK_TEXT_ON_ACCENT;
    v->style_focus.border_w     = 2;
    v->style_focus.opacity      = 255;

    return v;
}

/* ── Icon button ────────────────────────────────────────────── */

ui_view_t *ui_icon_button(const char *icon, int size,
                           void (*on_click)(ui_view_t *, int, int, void *),
                           void *ctx)
{
    ui_view_t *v = ui_view_create();
    if (!v) return NULL;

    v->text            = icon;
    v->style.fg        = TOK_TEXT_SECONDARY;
    v->style.font_px   = (uint8_t)(size > 24 ? 24 : size < 8 ? 8 : size);
    v->style.text_align = UI_TEXT_CENTER;
    v->style.radius    = TOK_RADIUS_SM;
    v->style.opacity   = 255;

    /* Hover: subtle bg */
    v->style_hover.bg      = GFX_RGB(30, 40, 60);
    v->style_hover.fg      = TOK_TEXT_PRIMARY;
    v->style_hover.opacity = 255;

    /* Active */
    v->style_active.bg      = GFX_RGB(18, 26, 42);
    v->style_active.opacity = 255;

    /* Fixed square hit-area slightly larger than the icon */
    int hit = size + 8;
    v->size.w_mode = UI_SIZE_FIXED; v->size.w = (int16_t)hit;
    v->size.h_mode = UI_SIZE_FIXED; v->size.h = (int16_t)hit;
    v->layout.align   = UI_ALIGN_CENTER;
    v->layout.justify = UI_JUST_CENTER;

    v->on_click  = on_click;
    v->event_ctx = ctx;
    v->focusable = 1;
    return v;
}

/* ── Text input ─────────────────────────────────────────────── */

ui_view_t *ui_input(char *buf, int buf_len,
                    const char *placeholder,
                    void (*on_change)(ui_view_t *, void *),
                    void *ctx)
{
    ui_view_t    *v = ui_view_create();
    input_data_t *d = input_alloc();
    if (!v) return NULL;

    if (d) {
        d->buf         = buf;
        d->buf_len     = buf_len;
        d->cursor      = (int)strlen(buf);
        d->placeholder = placeholder;
        d->on_change   = on_change;
        d->ctx         = ctx;
        v->userdata    = d;
    }

    v->style.bg           = GFX_RGB(16, 22, 36);
    v->style.fg           = TOK_TEXT_PRIMARY;
    v->style.radius       = TOK_RADIUS_SM;
    v->style.border_color = _COL_BORDER_SUBTLE;
    v->style.border_w     = 1;
    v->style.font_px      = 13;
    v->style.opacity      = 255;

    /* Focus: accent border */
    v->style_focus.bg           = GFX_RGB(14, 20, 34);
    v->style_focus.border_color = TOK_BORDER_FOCUS;
    v->style_focus.border_w     = 2;
    v->style_focus.opacity      = 255;

    /* Fixed height, fill width */
    v->size.h_mode = UI_SIZE_FIXED;
    v->size.h      = 36;
    v->size.w_mode = UI_SIZE_HUG;

    v->focusable = 1;
    v->on_paint  = input_paint;
    v->on_key    = input_key;
    return v;
}

/* ── Dividers ───────────────────────────────────────────────── */

ui_view_t *ui_divider_h(void)
{
    ui_view_t *v = ui_view_create();
    if (!v) return NULL;
    v->style.bg    = _COL_BORDER_SUBTLE;
    v->style.opacity = 255;
    v->size.w_mode = UI_SIZE_FILL;
    v->size.flex   = 1000;
    v->size.h_mode = UI_SIZE_FIXED;
    v->size.h      = 1;
    return v;
}

ui_view_t *ui_divider_v(void)
{
    ui_view_t *v = ui_view_create();
    if (!v) return NULL;
    v->style.bg    = _COL_BORDER_SUBTLE;
    v->style.opacity = 255;
    v->size.w_mode = UI_SIZE_FIXED;
    v->size.w      = 1;
    v->size.h_mode = UI_SIZE_FILL;
    return v;
}

/* ── Spacer ─────────────────────────────────────────────────── */

ui_view_t *ui_spacer(void)
{
    ui_view_t *v = ui_view_create();
    if (!v) return NULL;
    v->size.w_mode = UI_SIZE_FILL;
    v->size.flex   = 1000;
    v->size.h_mode = UI_SIZE_FILL;
    return v;
}

/* ── Card ───────────────────────────────────────────────────── */

ui_view_t *ui_card(int blur_r)
{
    ui_view_t  *v = ui_view_create();
    card_data_t *d = card_alloc();
    if (!v) return NULL;

    if (d && blur_r > 0) {
        d->blur_r     = blur_r;
        d->tint       = TOK_BG_SURFACE;
        d->tint_alpha = 178;   /* 70% — glass feel with depth */
        v->userdata   = d;
        v->on_paint   = card_paint;
        v->style.bg   = 0;    /* backdrop blur IS the background */
    } else {
        /* No blur: solid surface colour */
        v->style.bg = TOK_BG_SURFACE;
    }

    v->style.radius       = TOK_RADIUS_LG;
    v->style.shadow       = TOK_SHADOW_MD;
    v->style.border_color = _COL_BORDER_WINDOW;
    v->style.border_w     = 1;
    v->style.opacity      = 255;

    v->layout.direction = UI_DIR_COL;
    v->layout.align     = UI_ALIGN_STRETCH;
    v->size.w_mode      = UI_SIZE_HUG;
    v->size.h_mode      = UI_SIZE_HUG;
    return v;
}

/* ── Badge ──────────────────────────────────────────────────── */

ui_view_t *ui_badge(const char *text, uint32_t bg, uint32_t fg)
{
    ui_view_t *v = ui_view_create();
    if (!v) return NULL;

    v->text              = text;
    v->style.bg          = bg;
    v->style.fg          = fg;
    v->style.font_px     = 11;
    v->style.text_align  = UI_TEXT_CENTER;
    v->style.radius      = TOK_RADIUS_PILL;
    v->style.opacity     = 255;

    /* Tight pill padding: 2px top/bottom, 6px left/right */
    v->layout.pad_top    = 2;
    v->layout.pad_bottom = 2;
    v->layout.pad_left   = 6;
    v->layout.pad_right  = 6;
    v->layout.align      = UI_ALIGN_CENTER;
    v->layout.justify    = UI_JUST_CENTER;

    v->size.w_mode = UI_SIZE_HUG;
    v->size.h_mode = UI_SIZE_HUG;
    return v;
}
