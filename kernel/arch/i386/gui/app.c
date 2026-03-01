/* app.c — App registry, pin system, and launch dispatcher.
 *
 * 35+ apps organized in 7 categories.
 * Pinned apps appear in the radial launcher (max 8).
 * app_launch() dispatches to real implementations (settings, etc.)
 * or prints a placeholder message for unimplemented apps.
 */
#include <kernel/app.h>
#include <stdio.h>
#include <string.h>

/* ── App Registry ───────────────────────────────────────────────── */

static const app_info_t registry[] = {
    /* System — all implemented */
    { "terminal",   "Terminal",    "Tm", ICON_TERMINAL, 0xFF1E3A5F, APP_CAT_SYSTEM,   1, "bash,shell,cli,console,cmd,tty" },
    { "files",      "Files",       "Fl", ICON_FILES,    0xFF2E7D32, APP_CAT_SYSTEM,   1, "finder,explorer,folder,directory,browse" },
    { "settings",   "Settings",    "St", ICON_SETTINGS, 0xFFFF9500, APP_CAT_SYSTEM,   1, "preferences,config,options,system,control" },
    { "monitor",    "Monitor",     "Mo", ICON_MONITOR,  0xFF00C7BE, APP_CAT_SYSTEM,   1, "task,process,cpu,memory,performance,htop,top" },
    { "about",      "About",       "Ab", ICON_MONITOR,  0xFF263238, APP_CAT_SYSTEM,   0, "hardware,about,specs,cpu,ram,system,info" },

    /* Office — implemented */
    { "calculator", "Calculator",  "Ca", ICON_TABLE,    0xFFFF9500, APP_CAT_OFFICE,   0, "calc,math,add,multiply,numbers" },
    { "notes",      "Notes",       "Nt", ICON_PEN,      0xFFAF52DE, APP_CAT_OFFICE,   0, "memo,todo,list,notebook,jot,text,write" },

    /* Games — implemented */
    { "mines",      "Minesweeper", "Mi", ICON_GAMEPAD,  0xFF00C7BE, APP_CAT_GAMES,    0, "bomb,sweep,puzzle,grid,game" },
};

#define REGISTRY_COUNT ((int)(sizeof(registry) / sizeof(registry[0])))

/* ── Pin state ──────────────────────────────────────────────────── */

static int pin_slots[APP_MAX_PINNED];  /* index into registry, -1 = empty */
static int pin_count = 0;

/* ── Category metadata ──────────────────────────────────────────── */

static const char *cat_names[APP_CAT_COUNT] = {
    "System", "Internet", "Media", "Graphics", "Development", "Office", "Games"
};

static const uint32_t cat_colors[APP_CAT_COUNT] = {
    0xFF3478F6, 0xFF5856D6, 0xFFFF3B30,
    0xFFFF9500, 0xFF34C759, 0xFFAF52DE, 0xFF00C7BE
};

/* ── Public API ─────────────────────────────────────────────────── */

void app_init(void) {
    int i;
    pin_count = 0;
    for (i = 0; i < APP_MAX_PINNED; i++) pin_slots[i] = -1;

    /* Populate default pins */
    for (i = 0; i < REGISTRY_COUNT && pin_count < APP_MAX_PINNED; i++) {
        if (registry[i].default_pin) {
            pin_slots[pin_count++] = i;
        }
    }
}

int app_get_count(void) { return REGISTRY_COUNT; }

const app_info_t *app_get(int idx) {
    if (idx < 0 || idx >= REGISTRY_COUNT) return 0;
    return &registry[idx];
}

const app_info_t *app_find(const char *id) {
    int i;
    if (!id) return 0;
    for (i = 0; i < REGISTRY_COUNT; i++) {
        const char *a = registry[i].id, *b = id;
        int match = 1;
        while (*a || *b) {
            if (*a != *b) { match = 0; break; }
            a++; b++;
        }
        if (match) return &registry[i];
    }
    return 0;
}

/* Forward declarations for apps that have real implementations */
void app_settings_open_to(const char *tab);
void app_terminal_open(void);
void app_filemgr_open(void);
void app_monitor_open(void);
void app_taskmgr_open(void);
void app_calculator_open(void);
void app_notes_open(void);
void app_about_open(void);
void app_minesweeper_open(void);

void app_launch(const char *id) {
    if (!id) return;

    if (strcmp(id, "settings") == 0)    { app_settings_open_to("wallpaper"); return; }
    if (strcmp(id, "terminal") == 0)    { app_terminal_open(); return; }
    if (strcmp(id, "files") == 0)       { app_filemgr_open(); return; }
    if (strcmp(id, "monitor") == 0)     { app_taskmgr_open(); return; }
    if (strcmp(id, "about") == 0)       { app_about_open(); return; }
    if (strcmp(id, "calculator") == 0)  { app_calculator_open(); return; }
    if (strcmp(id, "notes") == 0)       { app_notes_open(); return; }
    if (strcmp(id, "mines") == 0)       { app_minesweeper_open(); return; }

    printf("[app] launch: %s\n", id);
}

int app_pin_count(void) { return pin_count; }

int app_pin_get(int slot) {
    if (slot < 0 || slot >= pin_count) return -1;
    return pin_slots[slot];
}

void app_pin_toggle(int app_idx) {
    int i;
    if (app_idx < 0 || app_idx >= REGISTRY_COUNT) return;

    /* Check if already pinned */
    for (i = 0; i < pin_count; i++) {
        if (pin_slots[i] == app_idx) {
            /* Remove: shift left */
            int j;
            for (j = i; j < pin_count - 1; j++)
                pin_slots[j] = pin_slots[j + 1];
            pin_slots[--pin_count] = -1;
            return;
        }
    }

    /* Pin if room */
    if (pin_count < APP_MAX_PINNED)
        pin_slots[pin_count++] = app_idx;
}

int app_is_pinned(int app_idx) {
    int i;
    for (i = 0; i < pin_count; i++)
        if (pin_slots[i] == app_idx) return 1;
    return 0;
}

void app_pin_reorder(int from_slot, int to_slot) {
    if (from_slot < 0 || from_slot >= pin_count) return;
    if (to_slot   < 0 || to_slot   >= pin_count) return;
    if (from_slot == to_slot) return;
    int tmp = pin_slots[from_slot];
    int dir = (to_slot > from_slot) ? 1 : -1;
    for (int i = from_slot; i != to_slot; i += dir)
        pin_slots[i] = pin_slots[i + dir];
    pin_slots[to_slot] = tmp;
}

const char *app_cat_name(int cat) {
    if (cat < 0 || cat >= APP_CAT_COUNT) return "";
    return cat_names[cat];
}

uint32_t app_cat_color(int cat) {
    if (cat < 0 || cat >= APP_CAT_COUNT) return 0xFF808080;
    return cat_colors[cat];
}
