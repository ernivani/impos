#ifndef _KERNEL_MONITOR_APP_H
#define _KERNEL_MONITOR_APP_H

#include <kernel/ui_widget.h>
#include <kernel/ui_event.h>

/* Open the System Monitor window (or bring to front). */
void app_monitor_open(void);

/* Per-frame tick. Returns 1 if mouse click consumed. */
int monitor_tick(int mx, int my, int btn_down, int btn_up);

/* Returns 1 if the Monitor window is open. */
int monitor_win_open(void);

/* Legacy API (misnamed as editor historically) */
void app_editor(void);
ui_window_t *app_editor_create(void);
void app_editor_on_event(ui_window_t *win, ui_event_t *ev);

#endif
