/* ui_window.h — UIKit window protocol (Phase 4)
 *
 * Drop-in-compatible replacement for wm2.h.
 * Same public interface, renamed to ui_window_*.
 *
 * Changes vs wm2:
 *  • Chrome drawn with ui_font (crisper title text)
 *  • Hit-test is a single clean function, not scattered conditionals
 *  • Drag state machine uses explicit enum
 *  • ~350 lines vs wm2.c's ~895
 *
 * Migration: in Phase 5, s/wm2_/ui_window_/ across callers and
 *            remove wm2.o from make.config.
 */

#ifndef _KERNEL_UI_WINDOW_H
#define _KERNEL_UI_WINDOW_H

#include <stdint.h>

/* ── Window states ──────────────────────────────────────────────── */
#define UI_WIN_NORMAL     0
#define UI_WIN_MAXIMIZED  1
#define UI_WIN_MINIMIZED  2

/* ── Hit-test region IDs ────────────────────────────────────────── */
#define UI_WIN_HIT_NONE       0
#define UI_WIN_HIT_CONTENT    1
#define UI_WIN_HIT_TITLEBAR   2
#define UI_WIN_HIT_BTN_CLOSE  3
#define UI_WIN_HIT_BTN_MIN    4
#define UI_WIN_HIT_BTN_MAX    5
#define UI_WIN_HIT_RESIZE_N   6
#define UI_WIN_HIT_RESIZE_S   7
#define UI_WIN_HIT_RESIZE_W   8
#define UI_WIN_HIT_RESIZE_E   9
#define UI_WIN_HIT_RESIZE_NW  10
#define UI_WIN_HIT_RESIZE_NE  11
#define UI_WIN_HIT_RESIZE_SW  12
#define UI_WIN_HIT_RESIZE_SE  13

/* ── Window info snapshot ───────────────────────────────────────── */
typedef struct {
    int  id;
    int  x, y, w, h;      /* outer screen bounds                   */
    int  cx, cy, cw, ch;  /* content area in screen coords         */
    char title[64];
    int  state;            /* UI_WIN_*                              */
    int  focused;
} ui_win_info_t;

/* ── Lifecycle ──────────────────────────────────────────────────── */
void ui_window_init(void);
int  ui_window_create(int x, int y, int w, int h, const char *title);
void ui_window_destroy(int id);

/* ── Management ─────────────────────────────────────────────────── */
void ui_window_focus(int id);
void ui_window_raise(int id);
void ui_window_move(int id, int x, int y);
void ui_window_resize(int id, int w, int h);
void ui_window_maximize(int id);
void ui_window_restore(int id);
void ui_window_minimize(int id);

/* ── Queries ────────────────────────────────────────────────────── */
int           ui_window_focused(void);
int           ui_window_count(void);
ui_win_info_t ui_window_info(int id);
int           ui_window_topmost_at(int mx, int my);

/* ── Canvas API ─────────────────────────────────────────────────── */
/* Returns the app-owned pixel area (below the title bar).
   Draw with gfx_surf_* using a gfx_surface_t{buf, cw, ch, cw}.    */
uint32_t *ui_window_canvas(int id, int *out_w, int *out_h);
void      ui_window_damage(int id, int x, int y, int w, int h);
void      ui_window_damage_all(int id);

/* ── Input ──────────────────────────────────────────────────────── */
void ui_window_mouse_event(int mx, int my, uint8_t btns, uint8_t prev_btns);
void ui_window_key_event(int id, char c);

/* ── Close ──────────────────────────────────────────────────────── */
int  ui_window_close_requested(int id);
void ui_window_close_clear(int id);
void ui_window_close_animated(int id);

/* ── Per-frame ──────────────────────────────────────────────────── */
void ui_window_tick(void);
void ui_window_redraw(int id);
void ui_window_redraw_all(void);

/* ── Overlay support ────────────────────────────────────────────── */
void ui_window_set_all_visible(int visible);

#endif /* _KERNEL_UI_WINDOW_H */
