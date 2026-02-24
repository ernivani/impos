#ifndef _KERNEL_COMPOSITOR_H
#define _KERNEL_COMPOSITOR_H

#include <stdint.h>
#include <kernel/gfx.h>

/* ═══ Layer IDs (z-order: 0 = bottom) ════════════════════════════ */

#define COMP_LAYER_WALLPAPER  0   /* static background, redrawn rarely */
#define COMP_LAYER_WINDOWS    1   /* WM client surfaces + decorations   */
#define COMP_LAYER_OVERLAY    2   /* toasts, overview, alt-tab          */
#define COMP_LAYER_CURSOR     3   /* always on top                      */
#define COMP_LAYER_COUNT      4

/* ═══ Surface ════════════════════════════════════════════════════ */

typedef struct comp_surface {
    uint32_t *pixels;     /* ARGB pixel buffer (owned by surface)       */
    int       w, h;       /* pixel dimensions                           */
    int       screen_x;   /* position in screen coordinates             */
    int       screen_y;
    uint8_t   alpha;      /* global surface opacity: 255 = opaque       */
    uint8_t   visible;
    uint8_t   layer;
    uint8_t   in_use;
    /* Damage rect in surface-local coordinates.
       damage_all overrides dmg_* and marks the whole surface dirty.   */
    uint8_t   damage_all;
    int       dmg_x, dmg_y, dmg_w, dmg_h;
} comp_surface_t;

/* ═══ Surface API ════════════════════════════════════════════════ */

/* Allocate a surface on the given layer.
   Returns NULL if pool is exhausted or layer is invalid.             */
comp_surface_t *comp_surface_create(int w, int h, int layer);

/* Free surface and its pixel buffer. Safe to call with NULL.         */
void comp_surface_destroy(comp_surface_t *s);

/* Move surface to (x, y) in screen coords.
   Automatically damages the old and new screen regions.              */
void comp_surface_move(comp_surface_t *s, int x, int y);

/* Change global opacity (255 = fully opaque, 0 = fully transparent). */
void comp_surface_set_alpha(comp_surface_t *s, uint8_t alpha);

/* Show or hide the surface (hidden surfaces are skipped in composite).*/
void comp_surface_set_visible(comp_surface_t *s, int visible);

/* Move surface to the front of its layer (drawn last = on top).      */
void comp_surface_raise(comp_surface_t *s);

/* Move surface to the back of its layer (drawn first = below others).*/
void comp_surface_lower(comp_surface_t *s);

/* ═══ Damage API ════════════════════════════════════════════════ */

/* Mark a region (surface-local coords) as needing recomposite.       */
void comp_surface_damage(comp_surface_t *s, int x, int y, int w, int h);

/* Mark the entire surface as dirty.                                  */
void comp_surface_damage_all(comp_surface_t *s);

/* ═══ Drawing API ═══════════════════════════════════════════════ */

/* Get a gfx_surface_t pointing at this surface's pixel buffer.
   Draw using gfx_surf_* functions, then call comp_surface_damage()
   for the region you modified.                                        */
gfx_surface_t comp_surface_lock(comp_surface_t *s);

/* Fill rect in surface coords and auto-damage the region.            */
void comp_surf_fill_rect(comp_surface_t *s, int x, int y, int w, int h, uint32_t color);

/* Draw string in surface coords and auto-damage.                     */
void comp_surf_draw_string(comp_surface_t *s, int x, int y,
                           const char *str, uint32_t fg, uint32_t bg);

/* Clear entire surface to color and mark damage_all.                 */
void comp_surf_clear(comp_surface_t *s, uint32_t color);

/* ═══ Compositor control ════════════════════════════════════════ */

/* Must be called once after gfx_init().                              */
void compositor_init(void);

/* Call once per frame from the desktop loop.
   Composites only damaged regions, then flips to the framebuffer.
   Respects a 60fps cap (returns immediately if called too soon).     */
void compositor_frame(void);

/* Force a full-screen recomposite on the next compositor_frame().   */
void compositor_damage_all(void);

/* Returns composites-per-second (updated once per second).           */
uint32_t compositor_get_fps(void);

#endif
