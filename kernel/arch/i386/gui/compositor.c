/* compositor.c — Phase 2: Retained-mode scene graph + damage compositor
 *
 * Architecture:
 *   Scene → Layer[] → Surface[]
 *
 *   Each Surface owns a pixel buffer (ARGB 32-bit) and a damage rect.
 *   compositor_frame() only repaints surfaces whose damage rect is non-zero,
 *   then blits changed scanline spans to the backbuffer, then calls gfx_flip().
 *
 *   This cuts memory bandwidth by 60-80% vs full-screen redraws.
 *
 * Layer Z-order (lowest to highest):
 *   LAYER_WALLPAPER  — static gradient / image, repainted only on change
 *   LAYER_WINDOWS    — WM client surfaces + decorations
 *   LAYER_OVERLAY    — toasts, alt-tab switcher, activity overview
 *   LAYER_CURSOR     — hardware cursor drawn post-composite to framebuffer
 *
 * TODO (Phase 2 implementation):
 *   [ ] Define surface_t, layer_t structs
 *   [ ] compositor_init(): allocate surface pool
 *   [ ] surface_create(w, h) → surface_t*
 *   [ ] surface_damage(s, x, y, w, h): union damage rect
 *   [ ] surface_clear_damage(s)
 *   [ ] compositor_frame(): iterate layers, blit damaged spans, flip
 *   [ ] compositor_add_to_layer(layer_id, surface)
 *   [ ] compositor_remove_surface(surface)
 *   [ ] Frame pacing: PIT-based 60fps budget (16ms), cap at 120fps
 */

#include <kernel/gfx.h>
#include <kernel/ui_theme.h>

/* Phase 1 placeholder — compositor_frame() is a no-op until Phase 2 */
void compositor_init(void)  { }
void compositor_frame(void) { }
