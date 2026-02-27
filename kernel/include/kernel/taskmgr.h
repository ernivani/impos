#ifndef _KERNEL_TASKMGR_H
#define _KERNEL_TASKMGR_H

#include <kernel/ui_widget.h>
#include <kernel/ui_event.h>

/* Open the Task Manager window (or bring to front). */
void app_taskmgr_open(void);

/* Per-frame tick. Returns 1 if mouse click consumed. */
int taskmgr_tick(int mx, int my, int btn_down, int btn_up);

/* Returns 1 if the Task Manager window is open. */
int taskmgr_win_open(void);

/* Legacy API */
void app_taskmgr(void);
ui_window_t *app_taskmgr_create(void);
void app_taskmgr_on_event(ui_window_t *win, ui_event_t *ev);
void app_taskmgr_on_tick(ui_window_t *win);

#endif
