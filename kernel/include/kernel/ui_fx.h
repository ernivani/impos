/* ui_fx.h — UIKit visual effects
 *
 * Three effects that close the visual gap between mockup.html and the kernel:
 *
 *   1. RGB box blur   — ui_fx_blur_rgb()
 *      Separable box blur on all three colour channels, O(w×h) per pass.
 *      Three passes approximate a Gaussian blur.
 *
 *   2. Drop shadows   — ui_fx_draw_shadow()
 *      Pre-computed per-call: render a rounded-rect alpha mask, box-blur it,
 *      composite as black with low opacity below and offset from the rect.
 *
 *   3. Backdrop blur  — ui_fx_backdrop_blur()
 *      Copy the region from the previous composite frame (backbuffer),
 *      blur it, and draw it as the surface background.
 *      Gives the frosted-glass window look from the mockup.
 *
 * Shadow levels (TOK_SHADOW_*):
 *   SM  — y+3  blur:6   opacity:35%   (context menus, tooltips)
 *   MD  — y+6  blur:14  opacity:45%   (windows, cards)
 *   LG  — y+12 blur:28  opacity:55%   (overlays, modals)
 */

#ifndef _KERNEL_UI_FX_H
#define _KERNEL_UI_FX_H

#include <stdint.h>
#include <kernel/gfx.h>

/* ── RGB blur ────────────────────────────────────────────────── */

/* In-place separable box blur of all R, G, B channels.
 * One call = one pass (cheap). Call 3× for Gaussian approximation.
 * Alpha channel is preserved unchanged.
 * radius: half-width of the blur kernel (1 = 3px, 6 = 13px, etc.) */
void ui_fx_blur_rgb(uint32_t *buf, int w, int h, int radius);

/* Convenience: run 3 passes for a near-Gaussian result */
void ui_fx_blur_rgb_3pass(uint32_t *buf, int w, int h, int radius);

/* ── Drop shadows ────────────────────────────────────────────── */

/* Draw a blurred drop shadow on surf beneath the given rect.
 * shadow_level: TOK_SHADOW_SM / MD / LG
 * The shadow is drawn onto the surface IN-PLACE; no extra allocation.
 * Call BEFORE drawing the view background so the shadow sits below it. */
void ui_fx_draw_shadow(gfx_surface_t *surf,
                       int x, int y, int w, int h,
                       int corner_r, int shadow_level);

/* ── Backdrop blur ───────────────────────────────────────────── */

/* Sample the compositor backbuffer at screen position (screen_x, screen_y),
 * blur the region, and write the result into surf starting at (dst_x, dst_y).
 * corner_r: clip the output to a rounded rectangle of this radius.
 * blur_r:   per-pass kernel radius (4–10 is typical for frosted glass).
 * Call BEFORE painting the surface's own content so it acts as the BG. */
void ui_fx_backdrop_blur(gfx_surface_t *surf,
                         int dst_x, int dst_y, int dst_w, int dst_h,
                         int screen_x, int screen_y,
                         int corner_r, int blur_r);

#endif /* _KERNEL_UI_FX_H */
