#ifndef _KERNEL_FILEMGR_H
#define _KERNEL_FILEMGR_H

#include <kernel/ui_widget.h>
#include <kernel/ui_event.h>

/* Open the Files window (or bring to front). */
void app_filemgr_open(void);

/* Per-frame tick. Returns 1 if mouse click consumed. */
int filemgr_tick(int mx, int my, int btn_down, int btn_up);

/* Returns 1 if the Files window is open. */
int filemgr_win_open(void);

/* Legacy API */
void app_filemgr(void);
ui_window_t *app_filemgr_create(void);
void app_filemgr_on_event(ui_window_t *win, ui_event_t *ev);
void app_filemgr_on_close(ui_window_t *win);

#endif
