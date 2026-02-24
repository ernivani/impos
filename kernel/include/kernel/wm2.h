#ifndef _KERNEL_WM2_H
#define _KERNEL_WM2_H

/* wm2.h — Phase 3: Window manager public API
 *
 * Each window owns one comp_surface_t on COMP_LAYER_WINDOWS.
 * WM draws decorations (macOS-style title bar, traffic-light buttons,
 * rounded corners) and blits the app's client pixel buffer into the
 * content area.  Apps never touch the compositor or gfx layer directly.
 */

#include <stdint.h>

/* ─── Window states ─────────────────────────────────────────────── */
#define WM2_STATE_NORMAL     0
#define WM2_STATE_MAXIMIZED  1
#define WM2_STATE_MINIMIZED  2

/* ─── Hit-test region IDs ───────────────────────────────────────── */
#define WM2_HIT_NONE       0
#define WM2_HIT_CONTENT    1
#define WM2_HIT_TITLEBAR   2
#define WM2_HIT_BTN_CLOSE  3   /* traffic-light close  (red)    */
#define WM2_HIT_BTN_MIN    4   /* traffic-light minimise (yellow) */
#define WM2_HIT_BTN_MAX    5   /* traffic-light maximise (green) */
#define WM2_HIT_RESIZE_N   6
#define WM2_HIT_RESIZE_S   7
#define WM2_HIT_RESIZE_W   8
#define WM2_HIT_RESIZE_E   9
#define WM2_HIT_RESIZE_NW  10
#define WM2_HIT_RESIZE_NE  11
#define WM2_HIT_RESIZE_SW  12
#define WM2_HIT_RESIZE_SE  13

/* ─── Window info snapshot ──────────────────────────────────────── */
typedef struct {
    int id;
    int x, y, w, h;      /* outer screen bounds                  */
    int cx, cy, cw, ch;  /* content area in screen coords        */
    char title[64];
    int state;           /* WM2_STATE_*                          */
    int focused;
} wm2_info_t;

/* ─── Lifecycle ─────────────────────────────────────────────────── */

/* Must be called once after compositor_init().                     */
void wm2_init(void);

/* Create a window.  Returns window id ≥ 0, or -1 on failure.      */
int  wm2_create(int x, int y, int w, int h, const char *title);

/* Destroy and free a window.                                       */
void wm2_destroy(int id);

/* ─── Window management ─────────────────────────────────────────── */

void wm2_focus(int id);
void wm2_raise(int id);
void wm2_move(int id, int x, int y);
void wm2_resize(int id, int w, int h);
void wm2_maximize(int id);
void wm2_restore(int id);
void wm2_minimize(int id);

/* ─── Queries ───────────────────────────────────────────────────── */

int        wm2_get_focused(void);
int        wm2_get_count(void);
wm2_info_t wm2_get_info(int id);

/* ─── Canvas API ────────────────────────────────────────────────── */

/* Returns the app's pixel buffer and its dimensions.
   Draw into it with gfx_surf_* helpers, then call wm2_damage_canvas*. */
uint32_t *wm2_get_canvas(int id, int *out_w, int *out_h);

/* Mark a sub-rect of the canvas dirty → triggers recomposite.     */
void wm2_damage_canvas(int id, int x, int y, int w, int h);

/* Mark the entire canvas dirty.                                    */
void wm2_damage_canvas_all(int id);

/* ─── Input routing ─────────────────────────────────────────────── */

/* Call once per frame.  buttons / prev_buttons use MOUSE_BTN_* flags. */
void wm2_mouse_event(int mx, int my, uint8_t buttons, uint8_t prev_buttons);

/* Route a key character to a specific window (Phase 5 will expand). */
void wm2_key_event(int id, char c);

/* ─── Close requests ────────────────────────────────────────────── */

/* Returns 1 if the close button was clicked on this window.        */
int  wm2_close_requested(int id);
void wm2_clear_close_request(int id);

/* ─── Force redraw ──────────────────────────────────────────────── */

void wm2_redraw(int id);
void wm2_redraw_all(void);

#endif
