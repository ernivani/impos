#ifndef _KERNEL_SETTINGS_APP_H
#define _KERNEL_SETTINGS_APP_H

#include <kernel/ui_widget.h>
#include <kernel/ui_event.h>

/* Open the settings window (or bring to front) at a specific tab.
   tab: "wallpaper", "appearance", "display", "about", or NULL for default. */
void app_settings_open_to(const char *tab);

/* Per-frame tick: handles close button and mouse events.
   Call from desktop_run() while settings window is open. */
void settings_tick(int mx, int my, int btn_up);

/* Returns 1 if the settings window is currently open. */
int settings_win_open(void);

/* Legacy API */
void app_settings(void);
ui_window_t *app_settings_create(void);
void app_settings_on_event(ui_window_t *win, ui_event_t *ev);

#endif
