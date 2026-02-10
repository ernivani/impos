#ifndef _KERNEL_SETTINGS_APP_H
#define _KERNEL_SETTINGS_APP_H

#include <kernel/ui_widget.h>
#include <kernel/ui_event.h>

void app_settings(void);

/* Multi-window API */
ui_window_t *app_settings_create(void);
void app_settings_on_event(ui_window_t *win, ui_event_t *ev);

#endif
