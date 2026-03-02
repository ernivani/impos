/* systray.c — System tray indicators in the menubar (Phase 8).
 *
 * Up to 8 indicator slots, each showing a 2-char abbreviation.
 * Items can update dynamically via on_update callbacks.
 * Rendered by menubar_paint(), hit-tested by menubar_mouse().
 */
#include <kernel/systray.h>
#include <string.h>

static systray_item_t items[SYSTRAY_MAX_ITEMS];

void systray_init(void) {
    memset(items, 0, sizeof(items));
}

int systray_register(const char *abbrev, const char *tooltip, uint32_t color,
                     void (*on_click)(int),
                     void (*on_update)(int, char *, uint32_t *)) {
    for (int i = 0; i < SYSTRAY_MAX_ITEMS; i++) {
        if (items[i].active) continue;
        items[i].active = 1;
        items[i].tooltip = tooltip;
        items[i].color = color;
        items[i].on_click = on_click;
        items[i].on_update = on_update;
        if (abbrev) {
            int j = 0;
            while (j < 3 && abbrev[j]) { items[i].abbrev[j] = abbrev[j]; j++; }
            items[i].abbrev[j] = '\0';
        }
        return i;
    }
    return -1;
}

void systray_unregister(int idx) {
    if (idx < 0 || idx >= SYSTRAY_MAX_ITEMS) return;
    items[idx].active = 0;
}

int systray_get_width(void) {
    int w = 0;
    for (int i = 0; i < SYSTRAY_MAX_ITEMS; i++)
        if (items[i].active) w += SYSTRAY_ITEM_W;
    return w;
}

int systray_get_count(void) {
    int c = 0;
    for (int i = 0; i < SYSTRAY_MAX_ITEMS; i++)
        if (items[i].active) c++;
    return c;
}

const systray_item_t *systray_get_item(int idx) {
    if (idx < 0 || idx >= SYSTRAY_MAX_ITEMS) return 0;
    if (!items[idx].active) return 0;
    return &items[idx];
}

int systray_click(int mx, int tray_x) {
    int cx = tray_x;
    for (int i = 0; i < SYSTRAY_MAX_ITEMS; i++) {
        if (!items[i].active) continue;
        if (mx >= cx && mx < cx + SYSTRAY_ITEM_W) {
            if (items[i].on_click)
                items[i].on_click(i);
            return 1;
        }
        cx += SYSTRAY_ITEM_W;
    }
    return 0;
}

void systray_update_all(void) {
    for (int i = 0; i < SYSTRAY_MAX_ITEMS; i++) {
        if (!items[i].active || !items[i].on_update) continue;
        items[i].on_update(i, items[i].abbrev, &items[i].color);
    }
}
