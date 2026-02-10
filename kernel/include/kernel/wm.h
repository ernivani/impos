#ifndef _KERNEL_WM_H
#define _KERNEL_WM_H

#include <stdint.h>

#define WM_MAX_WINDOWS  32
#define WM_TITLEBAR_H   28
#define WM_BORDER        1
#define WM_BTN_R         6
#define WM_RESIZE_ZONE   6

/* Window title bar colors (legacy — prefer ui_theme) */
#define WM_HEADER_BG       GFX_RGB(48, 48, 48)
#define WM_HEADER_FOCUSED  GFX_RGB(60, 60, 60)
#define WM_HEADER_TEXT     GFX_RGB(220, 220, 220)
#define WM_BODY_BG         GFX_RGB(30, 30, 30)
#define WM_BORDER_COLOR    GFX_RGB(20, 20, 20)
#define WM_CLOSE_HOVER     GFX_RGB(232, 17, 35)
#define WM_CLOSE_NORMAL    GFX_RGB(180, 60, 60)

/* Window flags */
#define WM_WIN_VISIBLE    0x0001
#define WM_WIN_FOCUSED    0x0002
#define WM_WIN_MINIMIZED  0x0004
#define WM_WIN_MAXIMIZED  0x0008
#define WM_WIN_RESIZABLE  0x0010

typedef struct {
    int  id;
    int  x, y, w, h;      /* outer bounds (including decorations) */
    char title[64];
    uint16_t flags;
    /* Legacy accessors — kept for compatibility */
    int  visible;
    int  focused;
    uint32_t *canvas;      /* pixel buffer for content area */
    int canvas_w, canvas_h;
    /* Saved geometry for restore from maximize */
    int restore_x, restore_y, restore_w, restore_h;
    /* Minimum window size */
    int min_w, min_h;
} wm_window_t;

void wm_initialize(void);
int  wm_create_window(int x, int y, int w, int h, const char *title);
void wm_destroy_window(int id);
void wm_focus_window(int id);
void wm_get_content_rect(int id, int *cx, int *cy, int *cw, int *ch);
wm_window_t* wm_get_window(int id);
int  wm_get_focused_id(void);

/* Minimize / maximize / restore */
void wm_minimize_window(int id);
void wm_maximize_window(int id);
void wm_restore_window(int id);
void wm_resize_window(int id, int new_w, int new_h);
int  wm_is_minimized(int id);
int  wm_is_maximized(int id);

/* Draw all windows + dock + background to backbuffer, then flip.
   Mouse cursor is drawn on top to framebuffer. */
void wm_composite(void);

/* Process mouse events: drag, click close, focus, dock.
   Called from getchar() idle callback. Returns action if dock was clicked. */
void wm_mouse_idle(void);

/* Returns non-zero if the focused window's close button was clicked */
int  wm_close_was_requested(void);
void wm_clear_close_request(void);

/* Desktop dock actions (reuse from desktop.h) */
int  wm_get_dock_action(void);
void wm_clear_dock_action(void);

/* Alt+Tab window cycling */
void wm_cycle_focus(void);

/* Dock hover from mouse */
int  wm_get_dock_hover(void);

/* Window list accessors */
int  wm_get_window_count(void);
wm_window_t* wm_get_window_by_index(int idx);

/* Canvas drawing API (coordinates relative to content area) */
void wm_fill_rect(int win_id, int x, int y, int w, int h, uint32_t color);
void wm_draw_string(int win_id, int x, int y, const char *s, uint32_t fg, uint32_t bg);
void wm_draw_char(int win_id, int x, int y, char c, uint32_t fg, uint32_t bg);
void wm_put_pixel(int win_id, int x, int y, uint32_t color);
void wm_draw_rect(int win_id, int x, int y, int w, int h, uint32_t color);
void wm_draw_line(int win_id, int x0, int y0, int x1, int y1, uint32_t color);
void wm_clear_canvas(int win_id, uint32_t color);
uint32_t *wm_get_canvas(int win_id, int *out_w, int *out_h);

/* Background draw callback (called by wm_composite after clearing) */
void wm_set_bg_draw(void (*fn)(void));

#endif
