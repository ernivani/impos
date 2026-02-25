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
    /* System */
    { "terminal",  "Terminal",    "Tm", ICON_TERMINAL, 0xFF1E3A5F, APP_CAT_SYSTEM,   1 },
    { "files",     "Files",       "Fl", ICON_FILES,    0xFF2E7D32, APP_CAT_SYSTEM,   1 },
    { "settings",  "Settings",    "St", ICON_SETTINGS, 0xFF4A4A8A, APP_CAT_SYSTEM,   1 },
    { "monitor",   "Monitor",     "Mo", ICON_MONITOR,  0xFF7B1F1F, APP_CAT_SYSTEM,   0 },
    { "disk",      "Disk Usage",  "Du", ICON_DISK,     0xFF5D4037, APP_CAT_SYSTEM,   0 },
    { "sysinfo",   "System Info", "Si", ICON_MONITOR,  0xFF263238, APP_CAT_SYSTEM,   0 },
    { "packages",  "Packages",    "Pk", ICON_DOWNLOAD, 0xFF1B5E20, APP_CAT_SYSTEM,   0 },
    { "users",     "Users",       "Us", ICON_USERS,    0xFF4527A0, APP_CAT_SYSTEM,   0 },
    { "logs",      "Logs",        "Lg", ICON_TERMINAL, 0xFF212121, APP_CAT_SYSTEM,   0 },

    /* Internet */
    { "browser",   "Browser",     "Br", ICON_BROWSER,  0xFF5856D6, APP_CAT_INTERNET, 1 },
    { "email",     "Email",       "Em", ICON_EMAIL,    0xFF5856D6, APP_CAT_INTERNET, 0 },
    { "chat",      "Chat",        "Ch", ICON_CHAT,     0xFF5856D6, APP_CAT_INTERNET, 0 },
    { "torrent",   "Torrent",     "To", ICON_DOWNLOAD, 0xFF5856D6, APP_CAT_INTERNET, 0 },
    { "ftp",       "FTP Client",  "Ft", ICON_FILES,    0xFF5856D6, APP_CAT_INTERNET, 0 },

    /* Media */
    { "music",     "Music",       "Mu", ICON_MUSIC,    0xFFFF3B30, APP_CAT_MEDIA,    0 },
    { "video",     "Video Player","Vd", ICON_VIDEO,    0xFFFF3B30, APP_CAT_MEDIA,    0 },
    { "podcasts",  "Podcasts",    "Po", ICON_MUSIC,    0xFFFF3B30, APP_CAT_MEDIA,    0 },
    { "recorder",  "Screen Rec.", "Sr", ICON_VIDEO,    0xFFFF3B30, APP_CAT_MEDIA,    0 },
    { "images",    "Image Viewer","Iv", ICON_IMAGE,    0xFFFF3B30, APP_CAT_MEDIA,    0 },
    { "radio",     "Radio",       "Ra", ICON_RADIO,    0xFFFF3B30, APP_CAT_MEDIA,    0 },

    /* Graphics */
    { "photoeditor","Photo Editor","Pe", ICON_IMAGE,   0xFFFF9500, APP_CAT_GRAPHICS, 0 },
    { "vectordraw","Vector Draw", "Vr", ICON_PEN,      0xFFFF9500, APP_CAT_GRAPHICS, 0 },
    { "screenshot","Screenshot",  "Sc", ICON_IMAGE,    0xFFFF9500, APP_CAT_GRAPHICS, 0 },
    { "colorpick", "Color Picker","Cp", ICON_IMAGE,    0xFFFF9500, APP_CAT_GRAPHICS, 0 },

    /* Development */
    { "code",      "Code Editor", "Ce", ICON_CODE,     0xFF34C759, APP_CAT_DEV,      1 },
    { "git",       "Git Client",  "Gc", ICON_CODE,     0xFF34C759, APP_CAT_DEV,      0 },
    { "database",  "Database",    "Db", ICON_TABLE,    0xFF34C759, APP_CAT_DEV,      0 },
    { "apitester", "API Tester",  "At", ICON_BROWSER,  0xFF34C759, APP_CAT_DEV,      0 },
    { "debugger",  "Debugger",    "Dg", ICON_CODE,     0xFF34C759, APP_CAT_DEV,      0 },

    /* Office */
    { "writer",    "Writer",      "Wr", ICON_PEN,      0xFFAF52DE, APP_CAT_OFFICE,   0 },
    { "spreadsheet","Spreadsheet","Sp", ICON_TABLE,    0xFFAF52DE, APP_CAT_OFFICE,   0 },
    { "presenter", "Presenter",   "Pr", ICON_PDF,      0xFFAF52DE, APP_CAT_OFFICE,   0 },
    { "pdfreader", "PDF Reader",  "Pd", ICON_PDF,      0xFFAF52DE, APP_CAT_OFFICE,   0 },
    { "notes",     "Notes",       "Nt", ICON_PEN,      0xFFAF52DE, APP_CAT_OFFICE,   0 },

    /* Games */
    { "solitaire", "Solitaire",   "So", ICON_GAMEPAD,  0xFF00C7BE, APP_CAT_GAMES,    0 },
    { "mines",     "Minesweeper", "Mi", ICON_GAMEPAD,  0xFF00C7BE, APP_CAT_GAMES,    0 },
    { "chess",     "Chess",       "Cs", ICON_GAMEPAD,  0xFF00C7BE, APP_CAT_GAMES,    0 },
    { "tetris",    "Tetris",      "Te", ICON_GAMEPAD,  0xFF00C7BE, APP_CAT_GAMES,    0 },
    { "snake",     "Snake",       "Sn", ICON_GAMEPAD,  0xFF00C7BE, APP_CAT_GAMES,    0 },
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

void app_launch(const char *id) {
    if (!id) return;

    /* Real apps */
    if (id[0]=='s' && id[1]=='e' && id[2]=='t') {
        /* settings */
        app_settings_open_to("wallpaper");
        return;
    }

    /* Everything else: log it */
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
