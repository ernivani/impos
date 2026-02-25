/* ui_font.h — UIKit unified font API
 *
 * Wraps three rendering tiers in priority order:
 *   1. TTF   — if a font file has been loaded via ui_font_load_ttf()
 *   2. Vec   — gfx_builtin_font_init() auto-traces the 8×16 bitmap font
 *              into vector rectangles and renders via gfx_path AA fill.
 *              Smooth at any size. No font file required.
 *   3. Scale — gfx_surf_draw_string_smooth() — integer-scale bitmap.
 *              Fallback if path library is unavailable.
 *
 * For Phase 2 the vec tier is always used (tier 1 requires initrd font data,
 * which is wired in Phase 5 when loading from disk).
 *
 * Align constants match CSS text-align semantics.
 */

#ifndef _KERNEL_UI_FONT_H
#define _KERNEL_UI_FONT_H

#include <stdint.h>
#include <kernel/gfx.h>
#include <kernel/gfx_ttf.h>

/* Text alignment */
#define UI_FONT_LEFT    0
#define UI_FONT_CENTER  1
#define UI_FONT_RIGHT   2

/* ── Init ────────────────────────────────────────────────────── */

/* Call once at boot, after gfx_init().
 * Initialises the vector font (no file needed).                 */
void ui_font_init(void);

/* Optional: load a TTF font file for higher-fidelity rendering.
 * data / len point to the raw .ttf bytes (e.g. from initrd).
 * Returns 1 on success, 0 on failure (vec font remains active). */
int  ui_font_load_ttf(const uint8_t *data, uint32_t len);

/* ── Metrics ─────────────────────────────────────────────────── */

/* Pixel width of string at the given size. */
int ui_font_width(const char *str, int px);

/* Line height (ascender + descender) at the given size. */
int ui_font_height(int px);

/* ── Drawing ─────────────────────────────────────────────────── */

/* Draw string at pixel position (x, y = top-left of glyph box). */
void ui_font_draw(gfx_surface_t *surf, int x, int y,
                  const char *str, uint32_t color, int px);

/* Draw string aligned inside a rect.
 * align: UI_FONT_LEFT / CENTER / RIGHT
 * Vertically centred in (ry, rh). */
void ui_font_draw_in_rect(gfx_surface_t *surf,
                          int rx, int ry, int rw, int rh,
                          const char *str, uint32_t color,
                          int px, int align);

/* Trim string to fit within max_w pixels, truncating with "…"
 * Writes result into buf (out_len bytes).                       */
void ui_font_ellipsis(const char *str, int px, int max_w,
                      char *buf, int buf_len);

#endif /* _KERNEL_UI_FONT_H */
