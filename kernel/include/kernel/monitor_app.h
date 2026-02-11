#ifndef _KERNEL_MONITOR_APP_H
#define _KERNEL_MONITOR_APP_H

#include <kernel/ui_widget.h>
#include <kernel/ui_event.h>

/* Graphical text editor (repurposed from System Monitor) */
void app_editor(void);

/* Multi-window API */
ui_window_t *app_editor_create(void);
void app_editor_on_event(ui_window_t *win, ui_event_t *ev);

#endif
