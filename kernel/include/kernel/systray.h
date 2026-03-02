#ifndef _KERNEL_SYSTRAY_H
#define _KERNEL_SYSTRAY_H

#include <stdint.h>

#define SYSTRAY_MAX_ITEMS 8
#define SYSTRAY_ITEM_W    28

typedef struct {
    char abbrev[4];       /* 2-char abbreviation + null */
    const char *tooltip;
    uint32_t color;
    void (*on_click)(int idx);
    void (*on_update)(int idx, char *abbrev_out, uint32_t *color_out);
    int active;
} systray_item_t;

void systray_init(void);
int  systray_register(const char *abbrev, const char *tooltip, uint32_t color,
                      void (*on_click)(int),
                      void (*on_update)(int, char *, uint32_t *));
void systray_unregister(int idx);
int  systray_get_width(void);
int  systray_get_count(void);
const systray_item_t *systray_get_item(int idx);
int  systray_click(int mx, int tray_x);
void systray_update_all(void);

#endif
