#ifndef _KERNEL_DESKTOP_H
#define _KERNEL_DESKTOP_H

#include <stdint.h>
#include <kernel/gfx.h>
#include <kernel/ui_theme.h>

/* Theme color aliases (legacy — prefer ui_theme.* directly) */
#define DT_BG            (ui_theme.desktop_bg)
#define DT_SURFACE       (ui_theme.surface)
#define DT_BORDER        (ui_theme.border)
#define DT_TEXT           (ui_theme.text_primary)
#define DT_TEXT_DIM       (ui_theme.text_dim)
#define DT_TEXT_MED       (ui_theme.text_secondary)
#define DT_TEXT_SUB       (ui_theme.text_sub)
#define DT_WIN_BG         (ui_theme.win_bg)
#define DT_TASKBAR_BG    (ui_theme.taskbar_bg)
#define DT_DOCK_PILL     (ui_theme.dock_pill_bg)
#define DT_ICON          (ui_theme.icon)
#define DT_ICON_HI       (ui_theme.icon_hi)
#define DT_ICON_DIM      (ui_theme.icon_dim)
#define DT_DOT           (ui_theme.dot)
#define DT_SEL_BG        (ui_theme.list_sel_bg)
#define DT_FIELD_BG      (ui_theme.input_bg)
#define DT_FIELD_BORDER  (ui_theme.input_border)
#define DT_FIELD_FOCUS   (ui_theme.input_border_focus)
#define DT_FIELD_PH      (ui_theme.input_placeholder)
#define DT_ERROR         (ui_theme.text_error)

/* Layout */
#define TASKBAR_H        (ui_theme.taskbar_height)

/* Compat aliases */
#define DOCK_H    TASKBAR_H
#define TITLE_H   0
#define WIN_PAD_X 0
#define WIN_PAD_Y 0
#define DOCK_GAP  0

/* Desktop actions returned by desktop_run() */
#define DESKTOP_ACTION_NONE     0
#define DESKTOP_ACTION_FILES    1
#define DESKTOP_ACTION_TERMINAL 2
#define DESKTOP_ACTION_BROWSER  3
#define DESKTOP_ACTION_EDITOR   4
#define DESKTOP_ACTION_SETTINGS 5
#define DESKTOP_ACTION_MONITOR  6
#define DESKTOP_ACTION_POWER    7
#define DESKTOP_ACTION_TRASH    8

void desktop_init(void);
void desktop_draw_dock(void);
void desktop_draw_menubar(void);
void desktop_draw_chrome(void);
int  desktop_run(void);
void desktop_open_terminal(void);
void desktop_close_terminal(void);
void desktop_notify_login(void);
void (*desktop_get_idle_terminal_cb(void))(void);

/* Dock geometry API — used by wm.c dock_hit() */
int  desktop_dock_y(void);
int  desktop_dock_h(void);
int  desktop_dock_x(void);
int  desktop_dock_w(void);
int  desktop_dock_items(void);
int  desktop_dock_sep_pos(void);
int  desktop_dock_item_rect(int idx, int *out_x, int *out_y, int *out_w, int *out_h);

/* Dynamic dock action for a given index (0 = running app, use focus) */
int  desktop_dock_action(int idx);

/* Request desktop icon refresh (deferred, checked in idle callback) */
void desktop_request_refresh(void);

/* ═══ Toast Notifications ═════════════════════════════════════════ */

#define TOAST_INFO     0
#define TOAST_SUCCESS  1
#define TOAST_WARNING  2
#define TOAST_ERROR    3

/* Show a toast notification (auto-dismisses after ~5 seconds).
   app_name: source app (e.g. "Settings", "Terminal", "Network")
   title: bold heading line
   message: detail text (can be NULL)
   type: TOAST_INFO / SUCCESS / WARNING / ERROR */
void toast_show(const char *app_name, const char *title, const char *message, int type);

/* Process mouse interaction with toasts (called from desktop idle).
   Returns 1 if the mouse event was consumed by a toast. */
int toast_handle_mouse(int mx, int my, int btn_down, int btn_held, int btn_up);

/* ═══ Alt-Tab Visual Switcher ═════════════════════════════════════ */

void alttab_activate(void);
void alttab_confirm(void);
void alttab_cancel(void);
int  alttab_is_visible(void);

#endif
