/* ui_widgets.h — UIKit widget library (Phase 3)
 *
 * Two layers on top of the raw ui_view_t:
 *
 *   1. Modifier API  — thin setters that return v, enabling concise
 *      inline construction:
 *        ui_view_append(row, ui_set_shadow(ui_button("OK", cb, ctx), 1));
 *
 *   2. Widget constructors — pre-styled views for the common controls.
 *      Every widget is a plain ui_view_t; modifiers work on them too.
 *
 * Convention: "set_" modifiers touch style/layout only; never allocate.
 *             Constructors may allocate from a static pool (input, card).
 */

#ifndef _KERNEL_UI_WIDGETS_H
#define _KERNEL_UI_WIDGETS_H

#include <kernel/ui_view.h>
#include <kernel/ui_token.h>
#include <kernel/ui_font.h>

/* ═══ Modifier API ═══════════════════════════════════════════════ */

/* ── Base style ─────────────────────────────────────────────── */
ui_view_t *ui_set_bg        (ui_view_t *v, uint32_t c);
ui_view_t *ui_set_fg        (ui_view_t *v, uint32_t c);
ui_view_t *ui_set_radius    (ui_view_t *v, int r);
ui_view_t *ui_set_shadow    (ui_view_t *v, int level);   /* TOK_SHADOW_* */
ui_view_t *ui_set_opacity   (ui_view_t *v, int alpha);   /* 0-255 */
ui_view_t *ui_set_border    (ui_view_t *v, uint32_t c, int w);
ui_view_t *ui_set_font      (ui_view_t *v, int px);
ui_view_t *ui_set_text_align(ui_view_t *v, int align);   /* UI_TEXT_* */

/* ── Pseudo-state overrides ─────────────────────────────────── */
ui_view_t *ui_set_hover_bg    (ui_view_t *v, uint32_t c);
ui_view_t *ui_set_hover_fg    (ui_view_t *v, uint32_t c);
ui_view_t *ui_set_active_bg   (ui_view_t *v, uint32_t c);
ui_view_t *ui_set_focus_border(ui_view_t *v, uint32_t c);
ui_view_t *ui_set_focus_bg    (ui_view_t *v, uint32_t c);

/* ── Size ───────────────────────────────────────────────────── */
ui_view_t *ui_set_w    (ui_view_t *v, int px);
ui_view_t *ui_set_h    (ui_view_t *v, int px);
ui_view_t *ui_set_wh   (ui_view_t *v, int w, int h);
ui_view_t *ui_set_fill (ui_view_t *v);              /* fill both axes */
ui_view_t *ui_set_fill_w(ui_view_t *v);
ui_view_t *ui_set_fill_h(ui_view_t *v);
ui_view_t *ui_set_hug  (ui_view_t *v);              /* shrink-wrap both */

/* ── Layout ─────────────────────────────────────────────────── */
ui_view_t *ui_set_pad    (ui_view_t *v, int px);
ui_view_t *ui_set_pad4   (ui_view_t *v, int t, int r, int b, int l);
ui_view_t *ui_set_gap    (ui_view_t *v, int px);
ui_view_t *ui_set_align  (ui_view_t *v, int a);     /* UI_ALIGN_* cross-axis */
ui_view_t *ui_set_justify(ui_view_t *v, int j);     /* UI_JUST_*  main-axis  */
ui_view_t *ui_set_clip   (ui_view_t *v);

/* ── Identity ───────────────────────────────────────────────── */
ui_view_t *ui_set_name(ui_view_t *v, const char *name);

/* ═══ Widget library ═════════════════════════════════════════════
 *
 * Call ui_widgets_init() once at startup (after ui_view_init).
 */

void ui_widgets_init(void);

/* ── Label ──────────────────────────────────────────────────── */

/* Read-only text.  Sizes to hug the text. */
ui_view_t *ui_label(const char *text, uint32_t fg, int px);

/* Secondary / dimmed label */
ui_view_t *ui_label_dim(const char *text, int px);

/* ── Buttons ────────────────────────────────────────────────── */

/* Ghost button: dark bg, subtle border, hover lift */
ui_view_t *ui_button(const char *label,
                     void (*on_click)(ui_view_t *, int, int, void *),
                     void *ctx);

/* Primary button: solid accent fill */
ui_view_t *ui_button_primary(const char *label,
                              void (*on_click)(ui_view_t *, int, int, void *),
                              void *ctx);

/* Destructive button: red fill */
ui_view_t *ui_button_danger(const char *label,
                             void (*on_click)(ui_view_t *, int, int, void *),
                             void *ctx);

/* Square icon-only button (transparent bg, hover circle) */
ui_view_t *ui_icon_button(const char *icon, int size,
                           void (*on_click)(ui_view_t *, int, int, void *),
                           void *ctx);

/* ── Text input ─────────────────────────────────────────────── */

/* Single-line editable text field.
 * buf / buf_len: caller-owned buffer (must stay alive while view exists).
 * placeholder: hint text shown when empty; may be NULL.
 * on_change: fired after every keystroke; may be NULL. */
ui_view_t *ui_input(char *buf, int buf_len,
                    const char *placeholder,
                    void (*on_change)(ui_view_t *, void *),
                    void *ctx);

/* ── Structural ─────────────────────────────────────────────── */

/* 1px horizontal separator */
ui_view_t *ui_divider_h(void);

/* 1px vertical separator */
ui_view_t *ui_divider_v(void);

/* Flexible gap — absorbs remaining space in its flex container */
ui_view_t *ui_spacer(void);

/* ── Card ───────────────────────────────────────────────────── */

/* Frosted-glass container.
 * blur_r: backdrop blur radius (4-10 typical; 0 = no blur, solid bg). */
ui_view_t *ui_card(int blur_r);

/* ── Badge ──────────────────────────────────────────────────── */

/* Small pill with text (notification count, status tag, etc.) */
ui_view_t *ui_badge(const char *text, uint32_t bg, uint32_t fg);

#endif /* _KERNEL_UI_WIDGETS_H */
