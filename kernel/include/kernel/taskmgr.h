#ifndef _KERNEL_TASKMGR_H
#define _KERNEL_TASKMGR_H

#include <kernel/ui_widget.h>
#include <kernel/ui_event.h>

void app_taskmgr(void);

/* Multi-window API */
ui_window_t *app_taskmgr_create(void);
void app_taskmgr_on_event(ui_window_t *win, ui_event_t *ev);

#endif
