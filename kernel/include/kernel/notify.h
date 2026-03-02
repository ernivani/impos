#ifndef _KERNEL_NOTIFY_H
#define _KERNEL_NOTIFY_H

#include <stdint.h>

#define NOTIFY_MAX_VISIBLE 3
#define NOTIFY_MAX_QUEUED  16

/* Urgency levels (accent colors) */
#define NOTIFY_INFO    0
#define NOTIFY_SUCCESS 1
#define NOTIFY_WARNING 2
#define NOTIFY_ERROR   3

/* Toast dimensions */
#define NOTIFY_W       300
#define NOTIFY_H       64
#define NOTIFY_MARGIN  8

typedef int notify_id_t;

void       notify_init(void);
notify_id_t notify_post(const char *title, const char *body,
                        int urgency, uint32_t timeout_ticks);
void       notify_dismiss(notify_id_t id);
void       notify_dismiss_all(void);
int        notify_visible_count(void);
void       notify_tick(uint32_t now);
int        notify_mouse(int mx, int my, int btn_down, int btn_up);

#endif
