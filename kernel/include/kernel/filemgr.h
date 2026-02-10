#ifndef _KERNEL_FILEMGR_H
#define _KERNEL_FILEMGR_H

#include <kernel/ui_widget.h>
#include <kernel/ui_event.h>

void app_filemgr(void);

/* Multi-window API */
ui_window_t *app_filemgr_create(void);
void app_filemgr_on_event(ui_window_t *win, ui_event_t *ev);
void app_filemgr_on_close(ui_window_t *win);

#endif
