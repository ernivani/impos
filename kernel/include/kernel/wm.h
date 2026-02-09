#ifndef _KERNEL_WM_H
#define _KERNEL_WM_H

#include <stdint.h>

#define WM_MAX_WINDOWS  8
#define WM_TITLEBAR_H   28
#define WM_BORDER        1
#define WM_BTN_R         6

/* Window title bar colors (GNOME-dark inspired) */
#define WM_HEADER_BG       GFX_RGB(48, 48, 48)
#define WM_HEADER_FOCUSED  GFX_RGB(60, 60, 60)
#define WM_HEADER_TEXT     GFX_RGB(220, 220, 220)
#define WM_BODY_BG         GFX_RGB(30, 30, 30)
#define WM_BORDER_COLOR    GFX_RGB(20, 20, 20)
#define WM_CLOSE_HOVER     GFX_RGB(232, 17, 35)
#define WM_CLOSE_NORMAL    GFX_RGB(180, 60, 60)

typedef struct {
    int  id;
    int  x, y, w, h;      /* outer bounds (including decorations) */
    char title[64];
    int  visible;
    int  focused;
} wm_window_t;

void wm_initialize(void);
int  wm_create_window(int x, int y, int w, int h, const char *title);
void wm_destroy_window(int id);
void wm_focus_window(int id);
void wm_get_content_rect(int id, int *cx, int *cy, int *cw, int *ch);
wm_window_t* wm_get_window(int id);
int  wm_get_focused_id(void);

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

#endif
