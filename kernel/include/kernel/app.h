#ifndef _KERNEL_APP_H
#define _KERNEL_APP_H

#include <stdint.h>
#include <kernel/icon_cache.h>

/* App categories */
#define APP_CAT_SYSTEM   0
#define APP_CAT_INTERNET 1
#define APP_CAT_MEDIA    2
#define APP_CAT_GRAPHICS 3
#define APP_CAT_DEV      4
#define APP_CAT_OFFICE   5
#define APP_CAT_GAMES    6
#define APP_CAT_COUNT    7

/* Maximum apps in registry */
#define APP_MAX 48

/* Maximum pinned apps (shown in radial) */
#define APP_MAX_PINNED 8

typedef struct {
    const char *id;         /* unique string id, e.g. "terminal" */
    const char *name;       /* display name, e.g. "Terminal" */
    const char *abbrev;     /* 2-letter fallback, e.g. "Tm" */
    int         icon_id;    /* ICON_* from icon_cache.h */
    uint32_t    color;      /* ARGB icon background color */
    int         category;   /* APP_CAT_* */
    int         default_pin;/* 1 if pinned by default */
    const char *keywords;   /* comma-separated search keywords */
} app_info_t;

/* Initialize app registry. */
void app_init(void);

/* Total number of apps in registry. */
int app_get_count(void);

/* Get app info by index [0, app_get_count()-1]. */
const app_info_t *app_get(int idx);

/* Find app by id string. Returns NULL if not found. */
const app_info_t *app_find(const char *id);

/* Launch an app by id. Stubs print to console; real apps open windows. */
void app_launch(const char *id);

/* ── Pin system ─────────────────────────────────────────────────── */

/* Returns number of pinned apps (0-APP_MAX_PINNED). */
int app_pin_count(void);

/* Returns index into app registry for pin slot [0, APP_MAX_PINNED-1]. */
int app_pin_get(int slot);

/* Toggle pin state for app at registry index. Enforces max. */
void app_pin_toggle(int app_idx);

/* Is app at registry index pinned? */
int app_is_pinned(int app_idx);

/* Move pinned slot 'from' before slot 'to' (drag-reorder). */
void app_pin_reorder(int from_slot, int to_slot);

/* ── Category names ─────────────────────────────────────────────── */
const char *app_cat_name(int cat);
uint32_t    app_cat_color(int cat);

#endif
