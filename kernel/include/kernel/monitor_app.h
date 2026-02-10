#ifndef _KERNEL_MONITOR_APP_H
#define _KERNEL_MONITOR_APP_H

#include <kernel/ui_widget.h>
#include <kernel/ui_event.h>

void app_monitor(void);

/* Multi-window API */
ui_window_t *app_monitor_create(void);
void app_monitor_on_event(ui_window_t *win, ui_event_t *ev);

#endif
