#include <kernel/desktop.h>
#include <kernel/gfx.h>
#include <kernel/tty.h>
#include <kernel/user.h>
#include <kernel/config.h>
#include <kernel/idt.h>
#include <kernel/io.h>
#include <kernel/mouse.h>
#include <kernel/acpi.h>
#include <kernel/wm.h>
#include <kernel/ui_theme.h>
#include <kernel/ui_widget.h>
#include <kernel/ui_event.h>
#include <kernel/shell.h>
#include <kernel/filemgr.h>
#include <kernel/taskmgr.h>
#include <kernel/settings_app.h>
#include <kernel/monitor_app.h>
#include <kernel/finder.h>
#include <kernel/task.h>
#include <kernel/fs.h>
#include <kernel/beep.h>
#include <string.h>
#include <stdio.h>

static int desktop_first_show = 1;

void desktop_notify_login(void) {
    desktop_first_show = 1;
}

/* ═══ Animation Helpers ═════════════════════════════════════════ */

/* Format a 2-digit value with leading zero */
static void fmt2(char *dst, int val) {
    dst[0] = '0' + (val / 10) % 10;
    dst[1] = '0' + val % 10;
}

/* ═══ Gradient Wallpaper ═══════════════════════════════════════ */

static uint32_t lerp_color(uint32_t a, uint32_t b, int t) {
    int r = (int)((a >> 16) & 0xFF) + (((int)((b >> 16) & 0xFF) - (int)((a >> 16) & 0xFF)) * t / 255);
    int g = (int)((a >> 8) & 0xFF) + (((int)((b >> 8) & 0xFF) - (int)((a >> 8) & 0xFF)) * t / 255);
    int bl = (int)(a & 0xFF) + (((int)(b & 0xFF) - (int)(a & 0xFF)) * t / 255);
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (bl < 0) bl = 0;
    if (bl > 255) bl = 255;
    return GFX_RGB(r, g, bl);
}

static uint32_t grad_tl, grad_tr, grad_bl, grad_br;

static void draw_gradient(int w, int h) {
    /* Slightly warmer/brighter than login gradient so
       the crossfade transition is visible */
    grad_tl = GFX_RGB(110, 90, 95);
    grad_tr = GFX_RGB(85, 70, 90);
    grad_bl = GFX_RGB(180, 130, 110);
    grad_br = GFX_RGB(130, 95, 110);

    uint32_t *bb = gfx_backbuffer();
    uint32_t pitch4 = gfx_pitch() / 4;

    for (int y = 0; y < h; y++) {
        int vy = y * 255 / (h - 1);
        uint32_t left  = lerp_color(grad_tl, grad_bl, vy);
        uint32_t right = lerp_color(grad_tr, grad_br, vy);
        for (int x = 0; x < w; x++) {
            int hx = x * 255 / (w - 1);
            bb[y * pitch4 + x] = lerp_color(left, right, hx);
        }
    }
}

/* ═══ Colored Dock Icons (20x20 area) ══════════════════════════ */

static void icon_folder(int x, int y, int sel) {
    uint32_t body = sel ? GFX_RGB(255, 200, 80) : GFX_RGB(220, 170, 55);
    uint32_t tab  = sel ? GFX_RGB(240, 180, 50) : GFX_RGB(190, 140, 40);
    uint32_t dark = GFX_RGB(160, 110, 30);
    uint32_t fold = sel ? GFX_RGB(255, 215, 120) : GFX_RGB(240, 190, 80);

    gfx_fill_rect(x + 2, y + 4, 7, 2, tab);
    gfx_fill_rect(x + 3, y + 7, 15, 10, dark);
    gfx_fill_rect(x + 2, y + 6, 15, 10, body);
    gfx_fill_rect(x + 2, y + 9, 15, 1, fold);
    gfx_fill_rect(x + 3, y + 10, 13, 5, dark);
}

static void icon_terminal(int x, int y, int sel) {
    uint32_t frame = sel ? GFX_RGB(100, 100, 100) : GFX_RGB(70, 70, 70);
    uint32_t inner = GFX_RGB(15, 15, 15);
    uint32_t green = sel ? GFX_RGB(100, 255, 140) : GFX_RGB(80, 220, 120);
    uint32_t title = GFX_RGB(30, 30, 30);

    gfx_draw_rect(x + 2, y + 2, 16, 16, frame);
    gfx_fill_rect(x + 3, y + 3, 14, 3, title);
    gfx_fill_circle(x + 5, y + 4, 1, GFX_RGB(255, 95, 87));
    gfx_fill_circle(x + 8, y + 4, 1, GFX_RGB(255, 189, 46));
    gfx_fill_circle(x + 11, y + 4, 1, GFX_RGB(39, 201, 63));
    gfx_fill_rect(x + 3, y + 6, 14, 11, inner);
    gfx_draw_line(x + 5, y + 9, x + 8, y + 12, green);
    gfx_draw_line(x + 5, y + 15, x + 8, y + 12, green);
    gfx_fill_rect(x + 10, y + 14, 5, 1, green);
}

static void icon_activity(int x, int y, int sel) {
    uint32_t c1 = sel ? GFX_RGB(100, 200, 255) : GFX_RGB(66, 150, 220);
    uint32_t c2 = sel ? GFX_RGB(80, 220, 140)  : GFX_RGB(60, 180, 110);
    uint32_t c3 = sel ? GFX_RGB(255, 160, 80)  : GFX_RGB(220, 130, 60);
    uint32_t base_c = sel ? GFX_RGB(120, 120, 120) : GFX_RGB(80, 80, 80);

    gfx_fill_rect(x + 2, y + 17, 16, 1, base_c);
    gfx_fill_rect(x + 3,  y + 10, 3, 7, c1);
    gfx_fill_rect(x + 7,  y + 5,  3, 12, c2);
    gfx_fill_rect(x + 11, y + 8,  3, 9, c3);
    gfx_fill_rect(x + 15, y + 3,  3, 14, c1);
}

static void icon_pencil(int x, int y, int sel) {
    uint32_t body = sel ? GFX_RGB(255, 175, 50) : GFX_RGB(230, 150, 30);
    uint32_t dark = GFX_RGB(180, 100, 20);
    uint32_t tip  = GFX_RGB(200, 200, 200);
    uint32_t eras = GFX_RGB(255, 120, 120);

    gfx_draw_line(x + 14, y + 4, x + 6, y + 14, body);
    gfx_draw_line(x + 13, y + 3, x + 5, y + 13, body);
    gfx_draw_line(x + 15, y + 4, x + 7, y + 14, dark);
    gfx_fill_rect(x + 13, y + 2, 3, 3, eras);
    gfx_put_pixel(x + 4, y + 15, tip);
    gfx_put_pixel(x + 5, y + 14, tip);
    gfx_fill_rect(x + 3, y + 17, 8, 1, GFX_RGB(60, 60, 60));
}

static void icon_gear(int x, int y, int sel) {
    uint32_t main_c = sel ? GFX_RGB(200, 200, 200) : GFX_RGB(150, 150, 150);
    uint32_t dark   = sel ? GFX_RGB(140, 140, 140) : GFX_RGB(100, 100, 100);
    uint32_t center = sel ? GFX_RGB(230, 230, 230) : GFX_RGB(180, 180, 180);
    int cx2 = x + 10, cy2 = y + 10;

    gfx_circle_ring(cx2, cy2, 6, 1, dark);
    gfx_fill_rect(cx2 - 1, cy2 - 8, 3, 3, main_c);
    gfx_fill_rect(cx2 - 1, cy2 + 5, 3, 3, main_c);
    gfx_fill_rect(cx2 - 8, cy2 - 1, 3, 3, main_c);
    gfx_fill_rect(cx2 + 5, cy2 - 1, 3, 3, main_c);
    gfx_fill_rect(cx2 + 3, cy2 - 6, 3, 3, dark);
    gfx_fill_rect(cx2 - 6, cy2 - 6, 3, 3, dark);
    gfx_fill_rect(cx2 + 3, cy2 + 3, 3, 3, dark);
    gfx_fill_rect(cx2 - 6, cy2 + 3, 3, 3, dark);
    gfx_circle_ring(cx2, cy2, 3, 1, center);
    gfx_fill_circle(cx2, cy2, 1, GFX_RGB(40, 40, 40));
}


static void icon_power(int x, int y, int sel) {
    uint32_t col = sel ? GFX_RGB(255, 100, 100) : GFX_RGB(200, 70, 70);
    int cx2 = x + 10, cy2 = y + 10;

    gfx_circle_ring(cx2, cy2, 7, 1, col);
    gfx_fill_rect(cx2 - 2, cy2 - 8, 5, 5, 0);
    gfx_fill_rect(cx2, cy2 - 8, 1, 8, col);
}

/* ═══ Time helper ══════════════════════════════════════════════ */

static void get_time_str(char *buf) {
    datetime_t dt;
    config_get_datetime(&dt);
    fmt2(buf, dt.hour);
    buf[2] = ':';
    fmt2(buf + 3, dt.minute);
    buf[5] = '\0';
}

#define MENUBAR_H 28

/* Shutdown button hit area on menubar */
static int shutdown_btn_x, shutdown_btn_w = 20;

/* ═══ Live Clock Update (Phase 1) ═════════════════════════════ */

static char last_clock_str[6] = "";

static void desktop_update_clock(void) {
    char cur[6];
    get_time_str(cur);
    if (memcmp(cur, last_clock_str, 5) == 0) return;
    memcpy(last_clock_str, cur, 6);

    int fb_w = (int)gfx_width();
    /* Redraw gradient strip for menubar region */
    uint32_t *bb = gfx_backbuffer();
    uint32_t pitch4 = gfx_pitch() / 4;
    for (int y = 0; y < MENUBAR_H; y++) {
        int vy = y * 255 / ((int)gfx_height() - 1);
        uint32_t left  = lerp_color(grad_tl, grad_bl, vy);
        uint32_t right = lerp_color(grad_tr, grad_br, vy);
        for (int x = 0; x < fb_w; x++) {
            int hx = x * 255 / (fb_w - 1);
            bb[y * pitch4 + x] = lerp_color(left, right, hx);
        }
    }
    desktop_draw_menubar();
    wm_invalidate_bg(); /* Clock changed — refresh cached background */
    gfx_flip_rect(0, 0, fb_w, MENUBAR_H);
}

/* ═══ WiFi Icon ══════════════════════════════════════════════ */

static void draw_wifi_icon_small(int x, int y, uint32_t color) {
    int r3 = 7;
    for (int dy = -r3; dy <= 0; dy++)
        for (int dx = -r3; dx <= r3; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 > r3 * r3 || d2 < (r3 - 2) * (r3 - 2)) continue;
            if (dy > -2) continue;
            gfx_put_pixel(x + dx, y + dy, color);
        }
    int r2 = 4;
    for (int dy = -r2; dy <= 0; dy++)
        for (int dx = -r2; dx <= r2; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 > r2 * r2 || d2 < (r2 - 1) * (r2 - 1)) continue;
            if (dy > -1) continue;
            gfx_put_pixel(x + dx, y + dy, color);
        }
    gfx_fill_rect(x - 1, y + 1, 3, 2, color);
}

/* ═══ Top Menu Bar ═════════════════════════════════════════════ */

void desktop_draw_menubar(void) {
    int fb_w = (int)gfx_width();

    /* Semi-transparent dark bar */
    gfx_rounded_rect_alpha(0, 0, fb_w, MENUBAR_H, 0,
                           GFX_RGB(20, 20, 30), 180);

    /* Thin bottom border */
    gfx_fill_rect(0, MENUBAR_H - 1, fb_w, 1, GFX_RGB(55, 52, 62));

    /* Left: "ImposOS" label */
    int text_y = (MENUBAR_H - FONT_H) / 2;
    gfx_draw_string_nobg(14, text_y, "ImposOS", GFX_RGB(220, 220, 230));

    /* Right side: WiFi icon, username, clock */
    char clk[6];
    get_time_str(clk);

    int rx = fb_w - 14;

    /* Clock (rightmost) */
    int clk_w = (int)strlen(clk) * FONT_W;
    rx -= clk_w;
    gfx_draw_string_nobg(rx, text_y, clk, GFX_RGB(200, 200, 210));

    /* Username */
    rx -= 16;
    const char *user = user_get_current();
    if (!user) user = "user";
    int usr_w = (int)strlen(user) * FONT_W;
    rx -= usr_w;
    gfx_draw_string_nobg(rx, text_y, user, GFX_RGB(180, 178, 190));

    /* WiFi icon */
    rx -= 20;
    draw_wifi_icon_small(rx + 6, text_y + FONT_H - 4, GFX_RGB(180, 178, 190));

    /* Power/shutdown button */
    rx -= 22;
    shutdown_btn_x = rx;
    {
        int pcx = rx + 8, pcy = text_y + FONT_H / 2;
        uint32_t pc = GFX_RGB(200, 100, 100);
        /* Small circle with gap at top */
        for (int dy = -5; dy <= 5; dy++)
            for (int dx = -5; dx <= 5; dx++) {
                int d2 = dx * dx + dy * dy;
                if (d2 > 25 || d2 < 9) continue;
                if (dy < -2 && dx >= -1 && dx <= 1) continue; /* gap */
                gfx_put_pixel(pcx + dx, pcy + dy, pc);
            }
        /* Vertical line through gap */
        gfx_fill_rect(pcx, pcy - 5, 1, 5, pc);
    }
}

/* ═══ Trash icon ═══════════════════════════════════════════════ */

static void icon_trash(int x, int y, int sel) {
    uint32_t body = sel ? GFX_RGB(160, 160, 170) : GFX_RGB(120, 120, 130);
    uint32_t lid  = sel ? GFX_RGB(180, 180, 190) : GFX_RGB(140, 140, 150);
    uint32_t dark = GFX_RGB(80, 80, 90);

    /* Lid */
    gfx_fill_rect(x + 4, y + 3, 12, 2, lid);
    gfx_fill_rect(x + 7, y + 1, 6, 2, lid);
    /* Body */
    gfx_fill_rect(x + 5, y + 5, 10, 12, body);
    gfx_fill_rect(x + 5, y + 5, 10, 1, dark);
    /* Stripes */
    gfx_fill_rect(x + 7, y + 7, 1, 8, dark);
    gfx_fill_rect(x + 10, y + 7, 1, 8, dark);
    gfx_fill_rect(x + 13, y + 7, 1, 8, dark);
}

/* ═══ Dock ═════════════════════════════════════════════════════ */

typedef void (*icon_fn)(int x, int y, int sel);

/* Dynamic dock items: Files + running apps + Trash */
#define DOCK_MAX_ITEMS 12

typedef struct {
    const char *label;
    int action;
    icon_fn icon_draw;       /* NULL for running app — draw initial letter */
    int wm_id;               /* -1 for static items */
    int is_static;           /* 1 for Files/Trash, 0 for running apps */
    char initial;            /* first letter for running app icon */
} dock_item_t;

static dock_item_t dock_dynamic[DOCK_MAX_ITEMS];
static int dock_item_count = 2;

static void rebuild_dock_items(void);

/* Phase 2: macOS-style dock dimensions (icon-only, no text labels) */
#define DOCK_ITEM_W   44
#define DOCK_ITEM_GAP 6
#define DOCK_SEP_W    14
#define DOCK_PILL_H   48
#define DOCK_PAD      12
#define DOCK_BOTTOM_MARGIN 10
#define DOCK_PILL_R   20

static int dock_pill_x, dock_pill_y, dock_pill_w;

/* Separator position: between static Files and first running app (if any) */
static int dock_sep_pos = -1;

static void compute_dock_layout(int fb_w, int fb_h) {
    int sep_w = (dock_sep_pos >= 0) ? DOCK_SEP_W : 0;
    dock_pill_w = dock_item_count * DOCK_ITEM_W + (dock_item_count > 1 ? dock_item_count - 1 : 0) * DOCK_ITEM_GAP + sep_w + DOCK_PAD * 2;
    dock_pill_x = fb_w / 2 - dock_pill_w / 2;
    dock_pill_y = fb_h - DOCK_BOTTOM_MARGIN - DOCK_PILL_H;
}

int desktop_dock_y(void) { return dock_pill_y; }
int desktop_dock_h(void) { return DOCK_PILL_H; }
int desktop_dock_x(void) { return dock_pill_x; }
int desktop_dock_w(void) { return dock_pill_w; }
int desktop_dock_items(void) { return dock_item_count; }
int desktop_dock_sep_pos(void) { return dock_sep_pos; }

int desktop_dock_item_rect(int idx, int *out_x, int *out_y, int *out_w, int *out_h) {
    if (idx < 0 || idx >= dock_item_count) return 0;
    int ix = dock_pill_x + DOCK_PAD;
    for (int i = 0; i < idx; i++) {
        if (i == dock_sep_pos) ix += DOCK_SEP_W;
        ix += DOCK_ITEM_W + DOCK_ITEM_GAP;
    }
    if (idx == dock_sep_pos) ix += DOCK_SEP_W;
    if (out_x) *out_x = ix;
    if (out_y) *out_y = dock_pill_y;
    if (out_w) *out_w = DOCK_ITEM_W;
    if (out_h) *out_h = DOCK_PILL_H;
    return 1;
}

/* Get dock action for a given dock index */
int desktop_dock_action(int idx) {
    if (idx < 0 || idx >= dock_item_count) return 0;
    return dock_dynamic[idx].action;
}

/* ═══ App Registry (Phase 3) ═══════════════════════════════════ */

#define MAX_RUNNING_APPS 8

typedef struct {
    int active;
    int wm_id;
    int dock_index;           /* which dock icon (-1 if none) */
    ui_window_t *ui_win;      /* NULL for terminal */
    void (*on_event)(ui_window_t *, ui_event_t *);
    void (*on_close)(ui_window_t *);
    void (*on_tick)(ui_window_t *);  /* periodic callback (NULL = none) */
    int tick_interval;               /* PIT ticks between calls (0 = disabled) */
    uint32_t last_tick;              /* last tick timestamp */
    int is_terminal;
    int task_id;              /* task tracker id (-1 if none) */
} running_app_t;

static running_app_t running_apps[MAX_RUNNING_APPS];

/* Check if a dock index has a running app */
static int find_running_app_by_dock(int dock_idx) {
    for (int i = 0; i < MAX_RUNNING_APPS; i++) {
        if (running_apps[i].active && running_apps[i].dock_index == dock_idx)
            return i;
    }
    return -1;
}

static int find_running_app_by_wm(int wm_id) {
    for (int i = 0; i < MAX_RUNNING_APPS; i++) {
        if (running_apps[i].active && running_apps[i].wm_id == wm_id)
            return i;
    }
    return -1;
}

static int register_app_ex(int wm_id, int dock_idx, ui_window_t *ui_win,
                           void (*on_event)(ui_window_t *, ui_event_t *),
                           void (*on_close)(ui_window_t *),
                           void (*on_tick)(ui_window_t *),
                           int tick_interval,
                           const char *app_name);

static int register_app(int wm_id, int dock_idx, ui_window_t *ui_win,
                        void (*on_event)(ui_window_t *, ui_event_t *),
                        void (*on_close)(ui_window_t *),
                        const char *app_name) {
    return register_app_ex(wm_id, dock_idx, ui_win, on_event, on_close,
                           NULL, 0, app_name);
}

static int register_app_ex(int wm_id, int dock_idx, ui_window_t *ui_win,
                           void (*on_event)(ui_window_t *, ui_event_t *),
                           void (*on_close)(ui_window_t *),
                           void (*on_tick)(ui_window_t *),
                           int tick_interval,
                           const char *app_name) {
    for (int i = 0; i < MAX_RUNNING_APPS; i++) {
        if (!running_apps[i].active) {
            running_apps[i].active = 1;
            running_apps[i].wm_id = wm_id;
            running_apps[i].dock_index = dock_idx;
            running_apps[i].ui_win = ui_win;
            running_apps[i].on_event = on_event;
            running_apps[i].on_close = on_close;
            running_apps[i].on_tick = on_tick;
            running_apps[i].tick_interval = tick_interval;
            running_apps[i].last_tick = pit_get_ticks();
            running_apps[i].is_terminal = 0;
            running_apps[i].task_id = wm_get_task_id(wm_id);
            if (running_apps[i].task_id >= 0)
                task_set_name(running_apps[i].task_id, app_name);
            rebuild_dock_items();
            return i;
        }
    }
    return -1;
}

static void unregister_app(int idx) {
    if (idx >= 0 && idx < MAX_RUNNING_APPS) {
        /* Task lifecycle owned by WM — just clear the slot */
        running_apps[idx].active = 0;
        rebuild_dock_items();
    }
}

/* is_dock_app_running removed — dynamic dock handles running app display */

/* Rebuild dynamic dock items: Files + running apps + Trash */
static void rebuild_dock_items(void) {
    dock_item_count = 0;

    /* Files (always first) */
    dock_dynamic[dock_item_count].label = "Files";
    dock_dynamic[dock_item_count].action = DESKTOP_ACTION_FILES;
    dock_dynamic[dock_item_count].icon_draw = icon_folder;
    dock_dynamic[dock_item_count].wm_id = -1;
    dock_dynamic[dock_item_count].is_static = 1;
    dock_dynamic[dock_item_count].initial = 'F';
    dock_item_count++;

    /* Running apps */
    int has_running = 0;
    for (int i = 0; i < MAX_RUNNING_APPS && dock_item_count < DOCK_MAX_ITEMS - 1; i++) {
        if (!running_apps[i].active) continue;
        has_running = 1;
        dock_item_t *d = &dock_dynamic[dock_item_count];
        /* Determine label from task name */
        const char *name = "App";
        if (running_apps[i].task_id >= 0) {
            task_info_t *t = task_get(running_apps[i].task_id);
            if (t && t->active) name = t->name;
        }
        d->label = name;
        d->action = 0; /* running apps use wm_id focus, not action */
        d->icon_draw = 0; /* draw initial letter */
        d->wm_id = running_apps[i].wm_id;
        d->is_static = 0;
        d->initial = name[0];
        dock_item_count++;
    }

    /* Separator between Files and running apps (if any) */
    dock_sep_pos = has_running ? 0 : -1;

    /* Trash (always last) */
    dock_dynamic[dock_item_count].label = "Trash";
    dock_dynamic[dock_item_count].action = DESKTOP_ACTION_TRASH;
    dock_dynamic[dock_item_count].icon_draw = icon_trash;
    dock_dynamic[dock_item_count].wm_id = -1;
    dock_dynamic[dock_item_count].is_static = 1;
    dock_dynamic[dock_item_count].initial = 'T';
    dock_item_count++;

    /* Recompute layout */
    compute_dock_layout((int)gfx_width(), (int)gfx_height());
}

void desktop_draw_dock(void) {
    int fb_w = (int)gfx_width(), fb_h = (int)gfx_height();
    compute_dock_layout(fb_w, fb_h);

    /* Frosted glass pill background */
    gfx_rounded_rect_alpha(dock_pill_x, dock_pill_y, dock_pill_w, DOCK_PILL_H,
                           DOCK_PILL_R, GFX_RGB(42, 40, 48), 160);
    gfx_rounded_rect_outline(dock_pill_x, dock_pill_y, dock_pill_w, DOCK_PILL_H,
                             DOCK_PILL_R, GFX_RGB(85, 82, 94));

    /* Subtle inner highlight: 1px lighter line at top of pill interior */
    gfx_fill_rect(dock_pill_x + DOCK_PILL_R, dock_pill_y + 1,
                  dock_pill_w - 2 * DOCK_PILL_R, 1, GFX_RGB(65, 62, 74));

    int ix = dock_pill_x + DOCK_PAD;

    for (int i = 0; i < dock_item_count; i++) {
        if (i == dock_sep_pos) {
            /* Vertical separator line */
            gfx_fill_rect(ix + DOCK_SEP_W / 2, dock_pill_y + 10, 1, DOCK_PILL_H - 20,
                          GFX_RGB(85, 82, 94));
            ix += DOCK_SEP_W;
        }

        int hover = wm_get_dock_hover();
        int highlighted = (i == hover);

        /* Hover/selection highlight behind item */
        if (highlighted) {
            gfx_rounded_rect_alpha(ix + 2, dock_pill_y + 4, DOCK_ITEM_W - 4,
                                   DOCK_PILL_H - 8, 8,
                                   GFX_RGB(255, 255, 255), 25);
        }

        /* Center 20x20 icon in 44-wide cell, vertically centered in pill */
        int icon_x = ix + (DOCK_ITEM_W - 20) / 2;
        int icon_y = dock_pill_y + (DOCK_PILL_H - 20) / 2;

        if (dock_dynamic[i].icon_draw) {
            /* Static icon (Files/Trash) */
            dock_dynamic[i].icon_draw(icon_x, icon_y, highlighted);
        } else {
            /* Running app — draw rounded rect with initial letter */
            uint32_t bg_c = highlighted ? GFX_RGB(80, 130, 220) : GFX_RGB(60, 100, 180);
            int rw = 24, rh = 24;
            int rx = ix + (DOCK_ITEM_W - rw) / 2;
            int ry = dock_pill_y + (DOCK_PILL_H - rh) / 2;
            gfx_rounded_rect_alpha(rx, ry, rw, rh, 6, bg_c, 220);
            /* Initial letter centered */
            char ch = dock_dynamic[i].initial;
            if (ch >= 'a' && ch <= 'z') ch -= 32;
            int cx = rx + (rw - FONT_W) / 2;
            int cy = ry + (rh - FONT_H) / 2;
            gfx_draw_char_nobg(cx, cy, ch, GFX_RGB(255, 255, 255));
        }

        /* Running-app indicator dot below icon (for static items with running apps) */
        if (dock_dynamic[i].is_static && !dock_dynamic[i].icon_draw) {
            /* skip — running apps don't need dots */
        } else if (!dock_dynamic[i].is_static) {
            /* Running app — always show dot */
            int dot_x = ix + DOCK_ITEM_W / 2;
            int dot_y = dock_pill_y + DOCK_PILL_H - 6;
            gfx_fill_rect(dot_x - 1, dot_y - 1, 3, 3, GFX_RGB(255, 255, 255));
        }

        /* Tooltip on hover (floating label above dock item) */
        if (hover == i) {
            const char *label = dock_dynamic[i].label;
            int lw = (int)strlen(label) * FONT_W;
            int tip_w = lw + 12;
            int tip_h = FONT_H + 8;
            int tip_x = ix + DOCK_ITEM_W / 2 - tip_w / 2;
            int tip_y = dock_pill_y - tip_h - 6;
            gfx_rounded_rect_alpha(tip_x, tip_y, tip_w, tip_h, 6,
                                   GFX_RGB(30, 28, 36), 200);
            gfx_draw_string_nobg(tip_x + 6, tip_y + 4, label,
                                 GFX_RGB(230, 230, 240));
        }

        ix += DOCK_ITEM_W + DOCK_ITEM_GAP;
    }
}

/* ═══ Desktop Draw ══════════════════════════════════════════════ */

void desktop_init(void) {
    int fb_w = (int)gfx_width(), fb_h = (int)gfx_height();
    draw_gradient(fb_w, fb_h);
    desktop_draw_menubar();
    desktop_draw_dock();

    if (desktop_first_show) {
        desktop_first_show = 0;
        gfx_crossfade(8, 30);
    } else {
        gfx_flip();
    }
}

void desktop_draw_chrome(void) {
    desktop_draw_menubar();
    desktop_draw_dock();
    gfx_flip();
}

/* ═══ Desktop File Icons ═══════════════════════════════════════ */

#define DESKTOP_MAX_ICONS 16
#define DESKTOP_ICON_W    80
#define DESKTOP_ICON_H    80
#define DESKTOP_ICON_MARGIN_X  20
#define DESKTOP_ICON_MARGIN_Y  (MENUBAR_H + 12)
#define DESKTOP_ICON_COLS  ((int)((gfx_width() - 2 * DESKTOP_ICON_MARGIN_X) / DESKTOP_ICON_W))

typedef struct {
    char name[MAX_NAME_LEN];
    uint8_t type;           /* INODE_FILE / INODE_DIR */
    int grid_col, grid_row;
    uint8_t selected;
    uint8_t active;
} desktop_icon_t;

static desktop_icon_t desktop_icons[DESKTOP_MAX_ICONS];
static int desktop_icon_count;

/* Hover / Drag state */
static int hover_icon = -1;
static int drag_icon = -1;
static int drag_ox, drag_oy;       /* mouse offset within icon cell */
static int drag_screen_x, drag_screen_y; /* current drag position */

/* Marquee selection state */
static int marquee_active = 0;
static int marquee_x0, marquee_y0, marquee_x1, marquee_y1;

/* Desktop icon double-click timer */
static int dclick_icon = -1;
static uint32_t dclick_tick = 0;
static int dclick_was_drag = 0;  /* suppress double-click after drag */

/* Refresh flag */
static int desktop_refresh_pending = 0;

static void desktop_load_layout(void);
static void desktop_save_layout(void);

static void desktop_load_icons(void) {
    desktop_icon_count = 0;
    memset(desktop_icons, 0, sizeof(desktop_icons));

    /* Save cwd, navigate to ~/Desktop */
    uint32_t saved_cwd = fs_get_cwd_inode();
    const char *user = user_get_current();
    if (!user) return;

    char desktop_path[128];
    snprintf(desktop_path, sizeof(desktop_path), "/home/%s/Desktop", user);
    if (fs_change_directory(desktop_path) != 0) {
        fs_change_directory_by_inode(saved_cwd);
        return;
    }

    fs_dir_entry_info_t entries[DESKTOP_MAX_ICONS];
    int count = fs_enumerate_directory(entries, DESKTOP_MAX_ICONS, 0);

    int cols = DESKTOP_ICON_COLS;
    if (cols < 1) cols = 1;

    for (int i = 0; i < count && desktop_icon_count < DESKTOP_MAX_ICONS; i++) {
        /* Skip . and .. */
        if (entries[i].name[0] == '.') continue;

        desktop_icon_t *icon = &desktop_icons[desktop_icon_count];
        strncpy(icon->name, entries[i].name, MAX_NAME_LEN - 1);
        icon->type = entries[i].type;
        icon->grid_col = desktop_icon_count % cols;
        icon->grid_row = desktop_icon_count / cols;
        icon->selected = 0;
        icon->active = 1;
        desktop_icon_count++;
    }

    /* Restore cwd */
    fs_change_directory_by_inode(saved_cwd);

    /* Apply saved layout positions from .layout file */
    desktop_load_layout();
}

/* ═══ .layout Persistence ═════════════════════════════════════ */

typedef struct __attribute__((packed)) {
    char name[MAX_NAME_LEN];
    int16_t col, row;
} layout_entry_t;

static void desktop_load_layout(void) {
    uint32_t saved_cwd = fs_get_cwd_inode();
    const char *user = user_get_current();
    if (!user) return;

    char desktop_path[128];
    snprintf(desktop_path, sizeof(desktop_path), "/home/%s/Desktop", user);
    if (fs_change_directory(desktop_path) != 0) {
        fs_change_directory_by_inode(saved_cwd);
        return;
    }

    uint8_t buf[sizeof(layout_entry_t) * DESKTOP_MAX_ICONS];
    size_t sz = sizeof(buf);
    if (fs_read_file(".layout", buf, &sz) != 0 || sz == 0) {
        fs_change_directory_by_inode(saved_cwd);
        return;
    }

    int entry_count = (int)(sz / sizeof(layout_entry_t));
    layout_entry_t *entries = (layout_entry_t *)buf;

    for (int i = 0; i < desktop_icon_count; i++) {
        for (int j = 0; j < entry_count; j++) {
            if (strncmp(desktop_icons[i].name, entries[j].name, MAX_NAME_LEN) == 0) {
                desktop_icons[i].grid_col = entries[j].col;
                desktop_icons[i].grid_row = entries[j].row;
                break;
            }
        }
    }

    fs_change_directory_by_inode(saved_cwd);
}

static void desktop_save_layout(void) {
    uint32_t saved_cwd = fs_get_cwd_inode();
    const char *user = user_get_current();
    if (!user) return;

    char desktop_path[128];
    snprintf(desktop_path, sizeof(desktop_path), "/home/%s/Desktop", user);
    if (fs_change_directory(desktop_path) != 0) {
        fs_change_directory_by_inode(saved_cwd);
        return;
    }

    layout_entry_t entries[DESKTOP_MAX_ICONS];
    int count = 0;
    for (int i = 0; i < desktop_icon_count; i++) {
        if (!desktop_icons[i].active) continue;
        strncpy(entries[count].name, desktop_icons[i].name, MAX_NAME_LEN);
        entries[count].col = (int16_t)desktop_icons[i].grid_col;
        entries[count].row = (int16_t)desktop_icons[i].grid_row;
        count++;
    }

    /* Delete old .layout and recreate */
    fs_delete_file(".layout");
    fs_create_file(".layout", 0);
    fs_write_file(".layout", (const uint8_t *)entries,
                  (size_t)count * sizeof(layout_entry_t));

    fs_change_directory_by_inode(saved_cwd);
}

void desktop_request_refresh(void) {
    desktop_refresh_pending = 1;
}

static void desktop_draw_file_icon(int x, int y, uint8_t type, int selected) {
    if (type == INODE_DIR) {
        /* Folder icon (larger version) */
        uint32_t body = selected ? GFX_RGB(255, 200, 80) : GFX_RGB(220, 170, 55);
        uint32_t tab  = selected ? GFX_RGB(240, 180, 50) : GFX_RGB(190, 140, 40);
        uint32_t dark = GFX_RGB(160, 110, 30);
        gfx_fill_rect(x + 2, y + 2, 14, 4, tab);
        gfx_fill_rect(x + 1, y + 6, 30, 20, body);
        gfx_fill_rect(x + 2, y + 12, 28, 1, dark);
    } else {
        /* File icon */
        uint32_t body = selected ? GFX_RGB(200, 200, 220) : GFX_RGB(170, 170, 190);
        uint32_t dark = selected ? GFX_RGB(160, 160, 180) : GFX_RGB(130, 130, 150);
        gfx_fill_rect(x + 4, y + 2, 24, 24, body);
        gfx_draw_rect(x + 4, y + 2, 24, 24, dark);
        /* Dog-ear */
        gfx_fill_rect(x + 20, y + 2, 8, 8, dark);
        /* Lines */
        gfx_fill_rect(x + 8, y + 12, 16, 1, dark);
        gfx_fill_rect(x + 8, y + 16, 12, 1, dark);
        gfx_fill_rect(x + 8, y + 20, 14, 1, dark);
    }
}

static void desktop_draw_icons(void) {
    for (int i = 0; i < desktop_icon_count; i++) {
        desktop_icon_t *icon = &desktop_icons[i];
        if (!icon->active) continue;

        /* Skip dragged icon in normal position (drawn at cursor later) */
        if (i == drag_icon) continue;

        int x = DESKTOP_ICON_MARGIN_X + icon->grid_col * DESKTOP_ICON_W;
        int y = DESKTOP_ICON_MARGIN_Y + icon->grid_row * DESKTOP_ICON_H;

        /* Hover highlight */
        if (i == hover_icon && !icon->selected) {
            gfx_rounded_rect_alpha(x, y, DESKTOP_ICON_W, DESKTOP_ICON_H, 6,
                                    GFX_RGB(255, 255, 255), 20);
        }

        /* Selection highlight */
        if (icon->selected) {
            gfx_rounded_rect_alpha(x, y, DESKTOP_ICON_W, DESKTOP_ICON_H, 6,
                                    ui_theme.accent, 50);
        }

        /* Icon centered in cell */
        int icon_x = x + (DESKTOP_ICON_W - 32) / 2;
        int icon_y = y + 8;
        desktop_draw_file_icon(icon_x, icon_y, icon->type, icon->selected);

        /* Label below icon */
        int lw = (int)strlen(icon->name) * FONT_W;
        int lx = x + (DESKTOP_ICON_W - lw) / 2;
        if (lx < x + 2) lx = x + 2;
        int ly = y + DESKTOP_ICON_H - FONT_H - 4;

        /* Truncate label if too wide */
        char label[16];
        if (lw > DESKTOP_ICON_W - 4) {
            strncpy(label, icon->name, 8);
            label[8] = '.';
            label[9] = '.';
            label[10] = '\0';
            lw = (int)strlen(label) * FONT_W;
            lx = x + (DESKTOP_ICON_W - lw) / 2;
        } else {
            strncpy(label, icon->name, 15);
            label[15] = '\0';
        }

        gfx_draw_string_nobg(lx, ly, label,
                              icon->selected ? GFX_RGB(255, 255, 255) : GFX_RGB(220, 220, 230));
    }

    /* Draw marquee selection rectangle */
    if (marquee_active) {
        int mx0 = marquee_x0 < marquee_x1 ? marquee_x0 : marquee_x1;
        int my0 = marquee_y0 < marquee_y1 ? marquee_y0 : marquee_y1;
        int mx1 = marquee_x0 > marquee_x1 ? marquee_x0 : marquee_x1;
        int my1 = marquee_y0 > marquee_y1 ? marquee_y0 : marquee_y1;
        int mw = mx1 - mx0, mh = my1 - my0;
        if (mw > 0 && mh > 0) {
            gfx_rounded_rect_alpha(mx0, my0, mw, mh, 0,
                                    GFX_RGB(60, 120, 220), 40);
            gfx_draw_rect(mx0, my0, mw, mh, GFX_RGB(80, 140, 240));
        }
    }

    /* Draw dragged icon at cursor position */
    if (drag_icon >= 0 && drag_icon < desktop_icon_count) {
        desktop_icon_t *icon = &desktop_icons[drag_icon];
        int x = drag_screen_x - drag_ox;
        int y = drag_screen_y - drag_oy;

        gfx_rounded_rect_alpha(x, y, DESKTOP_ICON_W, DESKTOP_ICON_H, 6,
                                ui_theme.accent, 40);
        int icon_x = x + (DESKTOP_ICON_W - 32) / 2;
        int icon_y = y + 8;
        desktop_draw_file_icon(icon_x, icon_y, icon->type, 1);

        char label[16];
        strncpy(label, icon->name, 15);
        label[15] = '\0';
        int lw = (int)strlen(label) * FONT_W;
        int lx = x + (DESKTOP_ICON_W - lw) / 2;
        if (lx < x + 2) lx = x + 2;
        int ly = y + DESKTOP_ICON_H - FONT_H - 4;
        gfx_draw_string_nobg(lx, ly, label, GFX_RGB(255, 255, 255));
    }
}

static int desktop_hit_icon(int mx, int my) {
    for (int i = 0; i < desktop_icon_count; i++) {
        desktop_icon_t *icon = &desktop_icons[i];
        if (!icon->active) continue;
        int x = DESKTOP_ICON_MARGIN_X + icon->grid_col * DESKTOP_ICON_W;
        int y = DESKTOP_ICON_MARGIN_Y + icon->grid_row * DESKTOP_ICON_H;
        if (mx >= x && mx < x + DESKTOP_ICON_W &&
            my >= y && my < y + DESKTOP_ICON_H)
            return i;
    }
    return -1;
}

static void desktop_deselect_all_icons(void) {
    for (int i = 0; i < desktop_icon_count; i++)
        desktop_icons[i].selected = 0;
}

/* ═══ Context Menu ════════════════════════════════════════════ */

#define CTX_MAX_ITEMS   6
#define CTX_ITEM_H     24
#define CTX_PADDING      6
#define CTX_MENU_W     150

/* Context menu actions */
#define CTX_ACT_NONE        0
#define CTX_ACT_OPEN        1
#define CTX_ACT_RENAME      2
#define CTX_ACT_TRASH       3
#define CTX_ACT_CLOSE       4
#define CTX_ACT_REFRESH     5
#define CTX_ACT_NEW_FILE    6
#define CTX_ACT_NEW_FOLDER  7
#define CTX_ACT_EMPTY_TRASH 8

typedef struct {
    int visible;
    int x, y;
    int item_count;
    int hover;
    const char *items[CTX_MAX_ITEMS];
    int actions[CTX_MAX_ITEMS];
    int target_icon;    /* -1 if not icon */
    int target_dock;    /* -1 if not dock */
} context_menu_t;

static context_menu_t ctx_menu;

/* Rename state */
static int ctx_renaming = 0;
static int ctx_rename_icon = -1;
static char ctx_rename_buf[MAX_NAME_LEN];
static int ctx_rename_cursor;

static void ctx_close(void) {
    ctx_menu.visible = 0;
    ctx_menu.hover = -1;
}

static void ctx_show_icon(int icon_idx, int mx, int my) {
    ctx_close();
    ctx_menu.visible = 1;
    ctx_menu.x = mx;
    ctx_menu.y = my;
    ctx_menu.target_icon = icon_idx;
    ctx_menu.target_dock = -1;
    ctx_menu.item_count = 0;

    ctx_menu.items[0] = "Open";
    ctx_menu.actions[0] = CTX_ACT_OPEN;
    ctx_menu.items[1] = "Rename";
    ctx_menu.actions[1] = CTX_ACT_RENAME;
    ctx_menu.items[2] = "Move to Trash";
    ctx_menu.actions[2] = CTX_ACT_TRASH;
    ctx_menu.item_count = 3;
}

static void ctx_show_dock(int dock_idx, int mx, int my) {
    ctx_close();
    ctx_menu.visible = 1;
    ctx_menu.x = mx;
    ctx_menu.y = my;
    ctx_menu.target_icon = -1;
    ctx_menu.target_dock = dock_idx;
    ctx_menu.item_count = 0;

    if (dock_idx >= 0 && dock_idx < dock_item_count) {
        if (!dock_dynamic[dock_idx].is_static) {
            /* Running app — offer Close */
            ctx_menu.items[0] = "Close";
            ctx_menu.actions[0] = CTX_ACT_CLOSE;
            ctx_menu.item_count = 1;
        } else if (dock_dynamic[dock_idx].action == DESKTOP_ACTION_TRASH) {
            /* Trash */
            ctx_menu.items[0] = "Open";
            ctx_menu.actions[0] = CTX_ACT_OPEN;
            ctx_menu.items[1] = "Empty Trash";
            ctx_menu.actions[1] = CTX_ACT_EMPTY_TRASH;
            ctx_menu.item_count = 2;
        } else {
            ctx_menu.items[0] = "Open";
            ctx_menu.actions[0] = CTX_ACT_OPEN;
            ctx_menu.item_count = 1;
        }
    }
}

static void ctx_show_desktop(int mx, int my) {
    ctx_close();
    ctx_menu.visible = 1;
    ctx_menu.x = mx;
    ctx_menu.y = my;
    ctx_menu.target_icon = -1;
    ctx_menu.target_dock = -1;
    ctx_menu.item_count = 0;

    ctx_menu.items[0] = "New File";
    ctx_menu.actions[0] = CTX_ACT_NEW_FILE;
    ctx_menu.items[1] = "New Folder";
    ctx_menu.actions[1] = CTX_ACT_NEW_FOLDER;
    ctx_menu.items[2] = "Refresh";
    ctx_menu.actions[2] = CTX_ACT_REFRESH;
    ctx_menu.item_count = 3;
}

static void ctx_draw_rename(void);

/* ═══ Toast Notification System ═══════════════════════════════════ */

#define TOAST_MAX         5
#define TOAST_WIDTH     320
#define TOAST_HEIGHT     72
#define TOAST_GAP         6
#define TOAST_MARGIN     12
#define TOAST_DURATION  600   /* ticks at 120Hz = 5 seconds */
#define TOAST_SLIDE_IN   15   /* ticks for slide animation */
#define TOAST_SLIDE_OUT  10
#define TOAST_DISMISS_VEL 8   /* px per tick when swiped away */

typedef struct {
    char app_name[32];
    char title[48];
    char message[80];
    int type;
    uint32_t start_tick;
    int active;
    /* Dismiss state */
    int dismiss_offset;   /* mouse drag offset (positive = sliding right) */
    int dismissing;       /* 1 = auto-sliding out after swipe */
    /* Cached screen position for hit testing */
    int screen_x, screen_y;
} toast_t;

static toast_t toasts[TOAST_MAX];
static int toast_dragging = -1;  /* index of toast being dragged */
static int toast_drag_start_x;   /* mouse X at drag start */
static int toast_drag_start_off; /* dismiss_offset at drag start */

void toast_show(const char *app_name, const char *title, const char *message, int type) {
    /* Find a free slot, or evict the oldest */
    int slot = -1;
    uint32_t oldest_tick = 0xFFFFFFFF;
    int oldest_slot = 0;
    for (int i = 0; i < TOAST_MAX; i++) {
        if (!toasts[i].active) { slot = i; break; }
        if (toasts[i].start_tick < oldest_tick) {
            oldest_tick = toasts[i].start_tick;
            oldest_slot = i;
        }
    }
    if (slot < 0) slot = oldest_slot;

    toast_t *t = &toasts[slot];
    memset(t, 0, sizeof(*t));
    if (app_name) strncpy(t->app_name, app_name, 31);
    if (title) strncpy(t->title, title, 47);
    if (message) strncpy(t->message, message, 79);
    t->type = type;
    t->start_tick = pit_get_ticks();
    t->active = 1;
    wm_mark_dirty();
}

/* Draw a small colored dot icon for the notification type */
static void toast_draw_icon(int cx, int cy, int type) {
    uint32_t color;
    switch (type) {
        case TOAST_SUCCESS: color = GFX_RGB(46, 180, 67);  break;
        case TOAST_WARNING: color = GFX_RGB(230, 170, 34); break;
        case TOAST_ERROR:   color = GFX_RGB(230, 60, 55);  break;
        default:            color = ui_theme.accent;        break;
    }
    /* Filled circle r=5 */
    for (int dy = -5; dy <= 5; dy++)
        for (int dx = -5; dx <= 5; dx++)
            if (dx*dx + dy*dy <= 25)
                gfx_put_pixel(cx + dx, cy + dy, color);
    /* Inner highlight */
    for (int dy = -2; dy <= 0; dy++)
        for (int dx = -2; dx <= 0; dx++)
            if (dx*dx + dy*dy <= 2) {
                uint32_t hi = GFX_RGB(
                    ((color >> 16) & 0xFF) / 2 + 128,
                    ((color >> 8) & 0xFF) / 2 + 128,
                    (color & 0xFF) / 2 + 128);
                gfx_put_pixel(cx + dx - 1, cy + dy - 1, hi);
            }
}

/* Draw a small X button */
static void toast_draw_close(int cx, int cy, int hovered) {
    uint32_t c = hovered ? GFX_RGB(255, 100, 100) : GFX_RGB(140, 140, 150);
    for (int d = -3; d <= 3; d++) {
        gfx_put_pixel(cx + d, cy + d, c);
        gfx_put_pixel(cx + d, cy - d, c);
        if (d > -3 && d < 3) {
            gfx_put_pixel(cx + d + 1, cy + d, c);
            gfx_put_pixel(cx + d + 1, cy - d, c);
        }
    }
}

static void toast_draw_all(void) {
    uint32_t now = pit_get_ticks();
    int fb_w = (int)gfx_width();
    int base_y = MENUBAR_H + TOAST_MARGIN;
    int drawn = 0;
    int any_active = 0;
    int mx = mouse_get_x(), my = mouse_get_y();

    for (int i = 0; i < TOAST_MAX; i++) {
        toast_t *t = &toasts[i];
        if (!t->active) continue;

        uint32_t elapsed = now - t->start_tick;

        /* Auto-dismiss after timeout */
        if (elapsed > TOAST_DURATION && !t->dismissing) {
            t->dismissing = 1;
        }

        /* Animate dismiss (either timeout or user swipe) */
        if (t->dismissing) {
            t->dismiss_offset += TOAST_DISMISS_VEL;
            if (t->dismiss_offset > TOAST_WIDTH + TOAST_MARGIN + 20) {
                t->active = 0;
                if (toast_dragging == i) toast_dragging = -1;
                continue;
            }
        }

        any_active = 1;

        /* Slide-in animation */
        int slide_x = 0;
        if (elapsed < TOAST_SLIDE_IN) {
            int anim_range = TOAST_WIDTH + TOAST_MARGIN;
            slide_x = anim_range - (anim_range * (int)elapsed / TOAST_SLIDE_IN);
        }

        int total_offset = slide_x + t->dismiss_offset;
        int tx = fb_w - TOAST_WIDTH - TOAST_MARGIN + total_offset;
        int cur_y = base_y + drawn * (TOAST_HEIGHT + TOAST_GAP);

        /* Save for hit testing */
        t->screen_x = tx;
        t->screen_y = cur_y;

        /* Clip: skip if fully off-screen right */
        if (tx >= fb_w) { drawn++; continue; }

        /* --- Draw notification card --- */

        /* Background: frosted glass look */
        gfx_rounded_rect_alpha(tx, cur_y, TOAST_WIDTH, TOAST_HEIGHT,
                               10, GFX_RGB(35, 35, 48), 210);
        /* Subtle border */
        gfx_draw_rect(tx, cur_y, TOAST_WIDTH, TOAST_HEIGHT,
                       GFX_RGB(65, 65, 80));

        /* Type icon (left side) */
        toast_draw_icon(tx + 16, cur_y + 18, t->type);

        /* App name (small, dimmed, top-left after icon) */
        if (t->app_name[0]) {
            gfx_draw_string_nobg(tx + 28, cur_y + 8,
                                 t->app_name, GFX_RGB(140, 140, 155));
        }

        /* Close X button (top-right) */
        int close_cx = tx + TOAST_WIDTH - 14;
        int close_cy = cur_y + 14;
        int close_hovered = (mx >= close_cx - 8 && mx <= close_cx + 8 &&
                             my >= close_cy - 8 && my <= close_cy + 8);
        toast_draw_close(close_cx, close_cy, close_hovered);

        /* Title (bold white) */
        int text_x = tx + 28;
        int title_y = cur_y + 8 + FONT_H + 4;
        if (t->title[0]) {
            gfx_draw_string_nobg(text_x, title_y,
                                 t->title, GFX_RGB(240, 240, 248));
        }

        /* Message (dimmer, below title) */
        if (t->message[0]) {
            gfx_draw_string_nobg(text_x, title_y + FONT_H + 2,
                                 t->message, GFX_RGB(160, 160, 175));
        }

        drawn++;
    }

    if (any_active)
        wm_mark_dirty();
}

/* Handle mouse interactions with toast notifications.
   Returns 1 if event was consumed by toast system. */
int toast_handle_mouse(int mx, int my, int btn_down, int btn_held, int btn_up) {
    /* Drag in progress */
    if (toast_dragging >= 0) {
        toast_t *t = &toasts[toast_dragging];
        if (!t->active) { toast_dragging = -1; return 0; }

        if (btn_held) {
            /* Update drag offset */
            int dx = mx - toast_drag_start_x;
            t->dismiss_offset = toast_drag_start_off + (dx > 0 ? dx : 0);
            wm_mark_dirty();
            return 1;
        }
        if (btn_up) {
            /* Release: if dragged far enough, auto-dismiss; else snap back */
            if (t->dismiss_offset > TOAST_WIDTH / 3) {
                t->dismissing = 1;
            } else {
                t->dismiss_offset = 0;
            }
            toast_dragging = -1;
            wm_mark_dirty();
            return 1;
        }
    }

    /* Check click/drag start on a toast */
    if (btn_down) {
        for (int i = 0; i < TOAST_MAX; i++) {
            toast_t *t = &toasts[i];
            if (!t->active || t->dismissing) continue;
            if (mx >= t->screen_x && mx < t->screen_x + TOAST_WIDTH &&
                my >= t->screen_y && my < t->screen_y + TOAST_HEIGHT) {
                /* Check close button */
                int close_cx = t->screen_x + TOAST_WIDTH - 14;
                int close_cy = t->screen_y + 14;
                if (mx >= close_cx - 8 && mx <= close_cx + 8 &&
                    my >= close_cy - 8 && my <= close_cy + 8) {
                    t->dismissing = 1;
                    wm_mark_dirty();
                    return 1;
                }
                /* Start drag */
                toast_dragging = i;
                toast_drag_start_x = mx;
                toast_drag_start_off = t->dismiss_offset;
                return 1;
            }
        }
    }

    return 0;
}

static void ctx_post_composite(void) {
    toast_draw_all();
    ctx_draw_rename();
    if (!ctx_menu.visible) return;

    int menu_h = ctx_menu.item_count * CTX_ITEM_H + 2 * CTX_PADDING;

    /* Clamp to screen */
    int mx = ctx_menu.x, my = ctx_menu.y;
    if (mx + CTX_MENU_W > (int)gfx_width()) mx = (int)gfx_width() - CTX_MENU_W;
    if (my + menu_h > (int)gfx_height()) my = (int)gfx_height() - menu_h;

    /* Shadow */
    gfx_fill_rect(mx + 2, my + 2, CTX_MENU_W, menu_h, GFX_RGB(8, 8, 12));

    /* Background */
    gfx_fill_rect(mx, my, CTX_MENU_W, menu_h, GFX_RGB(40, 40, 55));
    gfx_draw_rect(mx, my, CTX_MENU_W, menu_h, GFX_RGB(70, 70, 90));

    /* Items */
    for (int i = 0; i < ctx_menu.item_count; i++) {
        int iy = my + CTX_PADDING + i * CTX_ITEM_H;
        int hovered = (i == ctx_menu.hover);
        if (hovered) {
            gfx_fill_rect(mx + 2, iy, CTX_MENU_W - 4, CTX_ITEM_H, ui_theme.accent);
        }
        uint32_t text_c = hovered ? GFX_RGB(255, 255, 255) : ui_theme.text_primary;
        uint32_t bg_c = hovered ? ui_theme.accent : GFX_RGB(40, 40, 55);
        gfx_draw_string(mx + 12, iy + (CTX_ITEM_H - FONT_H) / 2,
                          ctx_menu.items[i], text_c, bg_c);
    }
}

static int ctx_hit_test(int mx, int my) {
    if (!ctx_menu.visible) return -1;
    int menu_h = ctx_menu.item_count * CTX_ITEM_H + 2 * CTX_PADDING;
    int cx = ctx_menu.x, cy = ctx_menu.y;
    if (cx + CTX_MENU_W > (int)gfx_width()) cx = (int)gfx_width() - CTX_MENU_W;
    if (cy + menu_h > (int)gfx_height()) cy = (int)gfx_height() - menu_h;

    if (mx < cx || mx >= cx + CTX_MENU_W || my < cy || my >= cy + menu_h)
        return -1;

    int idx = (my - cy - CTX_PADDING) / CTX_ITEM_H;
    if (idx < 0 || idx >= ctx_menu.item_count) return -1;
    return idx;
}

static void ctx_update_hover(int mx, int my) {
    int old = ctx_menu.hover;
    ctx_menu.hover = ctx_hit_test(mx, my);
    if (ctx_menu.hover != old)
        wm_composite();
}

/* Draw rename overlay on a desktop icon */
static void ctx_draw_rename(void) {
    if (!ctx_renaming || ctx_rename_icon < 0) return;
    desktop_icon_t *icon = &desktop_icons[ctx_rename_icon];
    if (!icon->active) return;

    int x = DESKTOP_ICON_MARGIN_X + icon->grid_col * DESKTOP_ICON_W;
    int y = DESKTOP_ICON_MARGIN_Y + icon->grid_row * DESKTOP_ICON_H;
    int ly = y + DESKTOP_ICON_H - FONT_H - 4;

    /* Input box over label */
    int bw = DESKTOP_ICON_W;
    gfx_fill_rect(x, ly - 2, bw, FONT_H + 4, ui_theme.input_bg);
    gfx_draw_rect(x, ly - 2, bw, FONT_H + 4, ui_theme.accent);

    /* Text */
    int max_chars = (bw - 4) / FONT_W;
    char display[MAX_NAME_LEN];
    strncpy(display, ctx_rename_buf, sizeof(display) - 1);
    display[sizeof(display) - 1] = '\0';
    if ((int)strlen(display) > max_chars)
        display[max_chars] = '\0';

    gfx_draw_string(x + 2, ly, display, ui_theme.text_primary, ui_theme.input_bg);

    /* Cursor */
    int cx = x + 2 + ctx_rename_cursor * FONT_W;
    if (cx < x + bw - 2)
        gfx_fill_rect(cx, ly, 1, FONT_H, ui_theme.accent);
}

/* ═══ Mobile / Desktop View Mode ═══════════════════════════════ */

#define VIEW_DESKTOP 0
#define VIEW_MOBILE  1

static int desktop_view_mode = VIEW_DESKTOP;
static int mobile_selected = 0;

/* Mobile grid: 4 columns x 2 rows of large app cards */
#define MOBILE_COLS  4
#define MOBILE_ROWS  2
#define MOBILE_CARD_W 160
#define MOBILE_CARD_H 160
#define MOBILE_GAP    24

/* Mobile app grid — all launchable apps (independent of dock) */
#define MOBILE_APP_COUNT 7

static const char *mobile_labels[MOBILE_APP_COUNT] = {
    "Files", "Terminal", "Activity", "Editor",
    "Settings", "Trash", "Power"
};
static const int mobile_actions[MOBILE_APP_COUNT] = {
    DESKTOP_ACTION_FILES, DESKTOP_ACTION_TERMINAL, DESKTOP_ACTION_BROWSER,
    DESKTOP_ACTION_EDITOR, DESKTOP_ACTION_SETTINGS, DESKTOP_ACTION_TRASH,
    DESKTOP_ACTION_POWER
};
static icon_fn mobile_icons[MOBILE_APP_COUNT] = {
    icon_folder, icon_terminal, icon_activity, icon_pencil,
    icon_gear, icon_trash, icon_power
};

static void desktop_draw_mobile_view(void) {
    int fb_w = (int)gfx_width(), fb_h = (int)gfx_height();

    /* Draw gradient background */
    draw_gradient(fb_w, fb_h);

    /* Dark overlay */
    gfx_rounded_rect_alpha(0, 0, fb_w, fb_h, 0, GFX_RGB(10, 10, 20), 160);

    /* Title */
    const char *title = "Applications";
    int tw = (int)strlen(title) * FONT_W * 2;
    gfx_draw_string_scaled(fb_w / 2 - tw / 2, 60, title,
                            GFX_RGB(220, 220, 240), 2);

    /* Grid of app cards */
    int total_w = MOBILE_COLS * MOBILE_CARD_W + (MOBILE_COLS - 1) * MOBILE_GAP;
    int total_h = MOBILE_ROWS * MOBILE_CARD_H + (MOBILE_ROWS - 1) * MOBILE_GAP;
    int start_x = fb_w / 2 - total_w / 2;
    int start_y = fb_h / 2 - total_h / 2;

    for (int i = 0; i < MOBILE_APP_COUNT && i < MOBILE_COLS * MOBILE_ROWS; i++) {
        int col = i % MOBILE_COLS;
        int row = i / MOBILE_COLS;
        int cx = start_x + col * (MOBILE_CARD_W + MOBILE_GAP);
        int cy = start_y + row * (MOBILE_CARD_H + MOBILE_GAP);

        int selected = (i == mobile_selected);

        /* Card background */
        uint32_t card_bg = selected ? GFX_RGB(60, 58, 78) : GFX_RGB(38, 36, 50);
        gfx_rounded_rect_alpha(cx, cy, MOBILE_CARD_W, MOBILE_CARD_H,
                                12, card_bg, 200);

        /* Selection border */
        if (selected) {
            gfx_rounded_rect_outline(cx, cy, MOBILE_CARD_W, MOBILE_CARD_H,
                                      12, ui_theme.accent);
        }

        /* Icon centered in card (scaled up) */
        int icon_x = cx + (MOBILE_CARD_W - 40) / 2;
        int icon_y = cy + 30;
        mobile_icons[i](icon_x, icon_y, selected);

        /* Label below icon */
        const char *label = mobile_labels[i];
        int lw = (int)strlen(label) * FONT_W;
        int lx = cx + (MOBILE_CARD_W - lw) / 2;
        int ly = cy + MOBILE_CARD_H - 36;
        gfx_draw_string_nobg(lx, ly, label,
                              selected ? GFX_RGB(255, 255, 255) : GFX_RGB(180, 178, 200));
    }

    /* Hint */
    const char *hint = "Arrow keys: navigate  Enter: open  Super/Esc: back";
    int hw = (int)strlen(hint) * FONT_W;
    gfx_draw_string_nobg(fb_w / 2 - hw / 2, fb_h - 50, hint,
                          GFX_RGB(100, 98, 120));

    gfx_flip();
    gfx_draw_mouse_cursor(mouse_get_x(), mouse_get_y());
}

/* ═══ Background draw callback for WM ══════════════════════════ */

static void desktop_bg_draw(void) {
    int fb_w = (int)gfx_width(), fb_h = (int)gfx_height();
    if (desktop_view_mode == VIEW_MOBILE) {
        desktop_draw_mobile_view();
        return;
    }
    draw_gradient(fb_w, fb_h);
    desktop_draw_menubar();
    desktop_draw_icons();
}

/* ═══ Desktop Event Loop (WM-based, multi-window) ══════════════ */

static int active_terminal_win = -1;
static int terminal_close_pending = 0;

/* ═══ Unified Idle Callback (Phase 3) ═════════════════════════ */

static void desktop_unified_idle(void) {
    /* Only attribute ticks to WM when doing actual WM work */
    task_set_current(TASK_WM);
    wm_mouse_idle();
    desktop_update_clock();
    task_set_current(TASK_IDLE);

    /* Watchdog: check for killed apps (cheap checks stay as IDLE) */
    for (int i = 0; i < MAX_RUNNING_APPS; i++) {
        if (running_apps[i].active && running_apps[i].task_id >= 0 &&
            task_check_killed(running_apps[i].task_id)) {
            task_set_current(TASK_WM);
            if (running_apps[i].is_terminal) {
                shell_fg_app_t *fg = shell_get_fg_app();
                if (fg && fg->on_close) fg->on_close();
                desktop_close_terminal();
                unregister_app(i);
                wm_invalidate_bg();
                wm_composite();
            } else {
                if (running_apps[i].on_close && running_apps[i].ui_win)
                    running_apps[i].on_close(running_apps[i].ui_win);
                if (running_apps[i].ui_win)
                    ui_window_destroy(running_apps[i].ui_win);
                unregister_app(i);
                wm_invalidate_bg();
                wm_composite();
            }
            task_set_current(TASK_IDLE);
        }
    }

    /* Check close request from WM */
    if (wm_close_was_requested()) {
        wm_clear_close_request();
        ui_event_t ev;
        ev.type = UI_EVENT_CLOSE;
        ui_push_event(&ev);
        keyboard_request_force_exit();
        return;
    }

    /* Check dock action */
    int da = wm_get_dock_action();
    if (da) {
        ui_event_t ev;
        ev.type = UI_EVENT_DOCK;
        ev.dock.action = da;
        ui_push_event(&ev);
        keyboard_request_force_exit();
        return;
    }

    /* Detect mouse button transitions for widget dispatch */
    uint8_t btns = mouse_get_buttons();
    int mx = mouse_get_x(), my = mouse_get_y();
    static uint8_t prev_btns = 0;

    /* Toast mouse handling (swipe-to-dismiss, close button) */
    {
        int btn_down = (btns & MOUSE_BTN_LEFT) && !(prev_btns & MOUSE_BTN_LEFT);
        int btn_held = (btns & MOUSE_BTN_LEFT) && (prev_btns & MOUSE_BTN_LEFT);
        int btn_up   = !(btns & MOUSE_BTN_LEFT) && (prev_btns & MOUSE_BTN_LEFT);
        if (toast_handle_mouse(mx, my, btn_down, btn_held, btn_up)) {
            prev_btns = btns;
            return;  /* toast consumed the event */
        }
    }

    if ((btns & MOUSE_BTN_LEFT) && !(prev_btns & MOUSE_BTN_LEFT)) {
        ui_event_t ev;
        ev.type = UI_EVENT_MOUSE_DOWN;
        ev.mouse.x = mx;
        ev.mouse.y = my;
        ev.mouse.wx = 0;
        ev.mouse.wy = 0;
        ev.mouse.buttons = btns;
        ui_push_event(&ev);
        keyboard_request_force_exit();
    }
    if (!(btns & MOUSE_BTN_LEFT) && (prev_btns & MOUSE_BTN_LEFT)) {
        ui_event_t ev;
        ev.type = UI_EVENT_MOUSE_UP;
        ev.mouse.x = mx;
        ev.mouse.y = my;
        ev.mouse.wx = 0;
        ev.mouse.wy = 0;
        ev.mouse.buttons = btns;
        ui_push_event(&ev);
        keyboard_request_force_exit();
    }

    /* Shutdown button click on menubar */
    if ((btns & MOUSE_BTN_LEFT) && !(prev_btns & MOUSE_BTN_LEFT) &&
        my >= 0 && my < MENUBAR_H &&
        mx >= shutdown_btn_x && mx < shutdown_btn_x + shutdown_btn_w) {
        ui_event_t ev;
        ev.type = UI_EVENT_DOCK;
        ev.dock.action = DESKTOP_ACTION_POWER;
        ui_push_event(&ev);
        keyboard_request_force_exit();
    }

    /* Left click while context menu visible → close or select */
    if ((btns & MOUSE_BTN_LEFT) && !(prev_btns & MOUSE_BTN_LEFT) && ctx_menu.visible) {
        int hit = ctx_hit_test(mx, my);
        if (hit >= 0) {
            /* Push a special event for the menu action */
            ui_event_t cev;
            cev.type = UI_EVENT_DOCK; /* reuse dock event type */
            cev.dock.action = 100 + hit; /* encode menu index */
            ui_push_event(&cev);
            keyboard_request_force_exit();
        } else {
            ctx_close();
            wm_composite();
        }
    }

    /* Right-click → open context menu (only when not over a WM window) */
    if ((btns & MOUSE_BTN_RIGHT) && !(prev_btns & MOUSE_BTN_RIGHT)) {
        /* Check dock first (dock is always visible on top) */
        int dock_right_hit = -1;
        if (my >= dock_pill_y && my < dock_pill_y + DOCK_PILL_H &&
            mx >= dock_pill_x && mx < dock_pill_x + dock_pill_w) {
            for (int di = 0; di < dock_item_count; di++) {
                int dix, diy, diw, dih;
                if (desktop_dock_item_rect(di, &dix, &diy, &diw, &dih)) {
                    if (mx >= dix && mx < dix + diw && my >= diy && my < diy + dih) {
                        dock_right_hit = di;
                        break;
                    }
                }
            }
        }

        if (dock_right_hit >= 0) {
            ctx_show_dock(dock_right_hit, mx, my);
            wm_composite();
        } else if (wm_hit_test(mx, my) < 0) {
            /* Check desktop icons */
            int icon_hit = desktop_hit_icon(mx, my);
            if (icon_hit >= 0) {
                desktop_deselect_all_icons();
                desktop_icons[icon_hit].selected = 1;
                ctx_show_icon(icon_hit, mx, my);
            } else {
                /* Empty desktop area */
                ctx_show_desktop(mx, my);
            }
            wm_invalidate_bg();
            wm_composite();
        }
    }

    /* Update context menu hover on mouse move */
    if (ctx_menu.visible) {
        ctx_update_hover(mx, my);
    }

    /* Desktop icon hover (only when not over a WM window) */
    if (wm_hit_test(mx, my) < 0 && drag_icon < 0) {
        int new_hover = desktop_hit_icon(mx, my);
        if (new_hover != hover_icon) {
            hover_icon = new_hover;
            wm_invalidate_bg();
            wm_composite();
        }
    } else if (hover_icon >= 0 && drag_icon < 0) {
        hover_icon = -1;
        wm_invalidate_bg();
        wm_composite();
    }

    /* Drag-to-move or marquee: start on left-press */
    if ((btns & MOUSE_BTN_LEFT) && !(prev_btns & MOUSE_BTN_LEFT) &&
        drag_icon < 0 && !marquee_active && wm_hit_test(mx, my) < 0 && !ctx_menu.visible) {
        int hit = desktop_hit_icon(mx, my);
        if (hit >= 0) {
            /* Start icon drag */
            drag_icon = hit;
            int ix = DESKTOP_ICON_MARGIN_X + desktop_icons[hit].grid_col * DESKTOP_ICON_W;
            int iy = DESKTOP_ICON_MARGIN_Y + desktop_icons[hit].grid_row * DESKTOP_ICON_H;
            drag_ox = mx - ix;
            drag_oy = my - iy;
            drag_screen_x = mx;
            drag_screen_y = my;
            desktop_icons[hit].selected = 1;
        } else if (my > MENUBAR_H && my < dock_pill_y) {
            /* Start marquee on empty desktop area */
            marquee_active = 1;
            marquee_x0 = mx;
            marquee_y0 = my;
            marquee_x1 = mx;
            marquee_y1 = my;
            desktop_deselect_all_icons();
        }
    }

    /* Drag-to-move: update while dragging */
    if (drag_icon >= 0 && (btns & MOUSE_BTN_LEFT)) {
        drag_screen_x = mx;
        drag_screen_y = my;
        wm_invalidate_bg();
        wm_composite();
    }

    /* Marquee: update while active */
    if (marquee_active && (btns & MOUSE_BTN_LEFT)) {
        marquee_x1 = mx;
        marquee_y1 = my;
        /* Select icons whose grid rect intersects marquee */
        int sel_x0 = marquee_x0 < marquee_x1 ? marquee_x0 : marquee_x1;
        int sel_y0 = marquee_y0 < marquee_y1 ? marquee_y0 : marquee_y1;
        int sel_x1 = marquee_x0 > marquee_x1 ? marquee_x0 : marquee_x1;
        int sel_y1 = marquee_y0 > marquee_y1 ? marquee_y0 : marquee_y1;
        for (int i = 0; i < desktop_icon_count; i++) {
            if (!desktop_icons[i].active) continue;
            int ix = DESKTOP_ICON_MARGIN_X + desktop_icons[i].grid_col * DESKTOP_ICON_W;
            int iy = DESKTOP_ICON_MARGIN_Y + desktop_icons[i].grid_row * DESKTOP_ICON_H;
            int iw = DESKTOP_ICON_W, ih = DESKTOP_ICON_H;
            /* Check rect intersection */
            desktop_icons[i].selected =
                (ix < sel_x1 && ix + iw > sel_x0 &&
                 iy < sel_y1 && iy + ih > sel_y0);
        }
        wm_invalidate_bg();
        wm_composite();
    }

    /* Marquee: release → end marquee, keep selection */
    if (marquee_active && !(btns & MOUSE_BTN_LEFT)) {
        marquee_active = 0;
        wm_invalidate_bg();
        wm_composite();
    }

    /* Drag-to-move: release → compute new grid position */
    if (drag_icon >= 0 && !(btns & MOUSE_BTN_LEFT)) {
        int drop_x = drag_screen_x - drag_ox;
        int drop_y = drag_screen_y - drag_oy;

        int new_col = (drop_x - DESKTOP_ICON_MARGIN_X + DESKTOP_ICON_W / 2) / DESKTOP_ICON_W;
        int new_row = (drop_y - DESKTOP_ICON_MARGIN_Y + DESKTOP_ICON_H / 2) / DESKTOP_ICON_H;

        int cols = DESKTOP_ICON_COLS;
        if (new_col < 0) new_col = 0;
        if (new_col >= cols) new_col = cols - 1;
        if (new_row < 0) new_row = 0;

        /* Check for collision */
        int collision = 0;
        for (int i = 0; i < desktop_icon_count; i++) {
            if (i == drag_icon || !desktop_icons[i].active) continue;
            if (desktop_icons[i].grid_col == new_col && desktop_icons[i].grid_row == new_row) {
                collision = 1;
                break;
            }
        }

        if (!collision) {
            desktop_icons[drag_icon].grid_col = new_col;
            desktop_icons[drag_icon].grid_row = new_row;
            desktop_save_layout();
        }

        drag_icon = -1;
        dclick_was_drag = 1;   /* suppress double-click from this release */
        wm_invalidate_bg();
        wm_composite();
    }

    /* Smart desktop refresh */
    if (desktop_refresh_pending) {
        desktop_refresh_pending = 0;
        desktop_load_icons();
        wm_invalidate_bg();
        wm_composite();
    }

    prev_btns = btns;

    /* Check double-ctrl for finder */
    if (keyboard_check_double_ctrl()) {
        ui_event_t ev;
        ev.type = UI_EVENT_KEY_PRESS;
        ev.key.key = KEY_FINDER;
        ev.key.ctrl = 0;
        ev.key.alt = 0;
        ev.key.shift = 0;
        ui_push_event(&ev);
        keyboard_request_force_exit();
        return;
    }

    /* Check resize for all running app windows */
    for (int i = 0; i < MAX_RUNNING_APPS; i++) {
        if (running_apps[i].active && running_apps[i].ui_win) {
            ui_window_check_resize(running_apps[i].ui_win);
        }
    }

    /* Composite if any window is dirty — credit redraw to the app */
    int needs_composite = 0;
    for (int i = 0; i < MAX_RUNNING_APPS; i++) {
        if (running_apps[i].active && running_apps[i].ui_win &&
            running_apps[i].ui_win->dirty) {
            if (running_apps[i].task_id >= 0)
                task_set_current(running_apps[i].task_id);
            ui_window_redraw(running_apps[i].ui_win);
            needs_composite = 1;
        }
    }
    if (needs_composite || wm_is_dirty()) {
        task_set_current(TASK_WM);
        wm_composite();
    }

    /* Widget app periodic tick callbacks */
    {
        uint32_t now_tick = pit_get_ticks();
        for (int i = 0; i < MAX_RUNNING_APPS; i++) {
            if (running_apps[i].active && running_apps[i].on_tick &&
                running_apps[i].tick_interval > 0 && running_apps[i].ui_win) {
                if (now_tick - running_apps[i].last_tick >= (uint32_t)running_apps[i].tick_interval) {
                    running_apps[i].last_tick = now_tick;
                    if (running_apps[i].task_id >= 0)
                        task_set_current(running_apps[i].task_id);
                    running_apps[i].on_tick(running_apps[i].ui_win);
                    task_set_current(TASK_IDLE);
                }
            }
        }
    }

    /* Foreground app periodic tick */
    shell_fg_app_t *fg = shell_get_fg_app();
    if (fg && fg->on_tick && fg->tick_interval > 0) {
        static uint32_t last_fg_tick = 0;
        uint32_t now = pit_get_ticks();
        if (now - last_fg_tick >= (uint32_t)fg->tick_interval) {
            last_fg_tick = now;
            if (fg->task_id >= 0)
                task_set_current(fg->task_id);
            fg->on_tick();
            task_set_current(TASK_IDLE);
        }
    }

    task_set_current(TASK_IDLE);
}

/* Idle callback for terminal during blocking command execution.
   Mirrors desktop_unified_idle() so widget apps stay responsive. */
static void desktop_idle_terminal(void) {
    task_set_current(TASK_WM);
    wm_mouse_idle();
    desktop_update_clock();
    task_set_current(TASK_IDLE);

    /* Watchdog: check for killed apps */
    for (int i = 0; i < MAX_RUNNING_APPS; i++) {
        if (running_apps[i].active && running_apps[i].task_id >= 0 &&
            task_check_killed(running_apps[i].task_id)) {
            if (running_apps[i].is_terminal) {
                shell_fg_app_t *fg = shell_get_fg_app();
                if (fg && fg->on_close) fg->on_close();
                terminal_close_pending = 1;
                keyboard_request_force_exit();
            } else {
                if (running_apps[i].on_close && running_apps[i].ui_win)
                    running_apps[i].on_close(running_apps[i].ui_win);
                if (running_apps[i].ui_win)
                    ui_window_destroy(running_apps[i].ui_win);
                unregister_app(i);
                wm_invalidate_bg();
            }
        }
    }

    /* Check close request from WM */
    if (wm_close_was_requested()) {
        wm_clear_close_request();
        int fid = wm_get_focused_id();
        int ri = (fid >= 0) ? find_running_app_by_wm(fid) : -1;
        if (ri >= 0 && running_apps[ri].is_terminal) {
            terminal_close_pending = 1;
            keyboard_request_force_exit();
        } else if (ri >= 0) {
            if (running_apps[ri].on_close && running_apps[ri].ui_win)
                running_apps[ri].on_close(running_apps[ri].ui_win);
            if (running_apps[ri].ui_win)
                ui_window_destroy(running_apps[ri].ui_win);
            unregister_app(ri);
            wm_invalidate_bg();
        }
    }

    /* Check dock action */
    int da = wm_get_dock_action();
    if (da) {
        ui_event_t ev;
        ev.type = UI_EVENT_DOCK;
        ev.dock.action = da;
        ui_push_event(&ev);
        keyboard_request_force_exit();
        return;
    }

    /* Detect mouse button transitions for widget dispatch */
    uint8_t btns = mouse_get_buttons();
    int mx = mouse_get_x(), my = mouse_get_y();
    static uint8_t prev_btns_term = 0;

    if ((btns & MOUSE_BTN_LEFT) && !(prev_btns_term & MOUSE_BTN_LEFT)) {
        ui_event_t ev;
        ev.type = UI_EVENT_MOUSE_DOWN;
        ev.mouse.x = mx;
        ev.mouse.y = my;
        ev.mouse.wx = 0;
        ev.mouse.wy = 0;
        ev.mouse.buttons = btns;
        ui_push_event(&ev);
    }
    if (!(btns & MOUSE_BTN_LEFT) && (prev_btns_term & MOUSE_BTN_LEFT)) {
        ui_event_t ev;
        ev.type = UI_EVENT_MOUSE_UP;
        ev.mouse.x = mx;
        ev.mouse.y = my;
        ev.mouse.wx = 0;
        ev.mouse.wy = 0;
        ev.mouse.buttons = btns;
        ui_push_event(&ev);
    }
    prev_btns_term = btns;

    /* Check double-ctrl for finder */
    if (keyboard_check_double_ctrl()) {
        ui_event_t ev;
        ev.type = UI_EVENT_KEY_PRESS;
        ev.key.key = KEY_FINDER;
        ev.key.ctrl = 0;
        ev.key.alt = 0;
        ev.key.shift = 0;
        ui_push_event(&ev);
        keyboard_request_force_exit();
        return;
    }

    /* Check resize for all running widget app windows */
    for (int i = 0; i < MAX_RUNNING_APPS; i++) {
        if (running_apps[i].active && running_apps[i].ui_win)
            ui_window_check_resize(running_apps[i].ui_win);
    }

    /* Redraw dirty widget windows */
    int needs_composite = 0;
    for (int i = 0; i < MAX_RUNNING_APPS; i++) {
        if (running_apps[i].active && running_apps[i].ui_win &&
            running_apps[i].ui_win->dirty) {
            if (running_apps[i].task_id >= 0)
                task_set_current(running_apps[i].task_id);
            ui_window_redraw(running_apps[i].ui_win);
            needs_composite = 1;
        }
    }

    /* Widget app periodic tick callbacks */
    {
        uint32_t now_tick = pit_get_ticks();
        for (int i = 0; i < MAX_RUNNING_APPS; i++) {
            if (running_apps[i].active && running_apps[i].on_tick &&
                running_apps[i].tick_interval > 0 && running_apps[i].ui_win) {
                if (now_tick - running_apps[i].last_tick >= (uint32_t)running_apps[i].tick_interval) {
                    running_apps[i].last_tick = now_tick;
                    if (running_apps[i].task_id >= 0)
                        task_set_current(running_apps[i].task_id);
                    running_apps[i].on_tick(running_apps[i].ui_win);
                    task_set_current(TASK_IDLE);
                    needs_composite = 1;
                }
            }
        }
    }

    /* Throttled composite — only when something actually changed */
    static uint32_t last_composite_tick = 0;
    uint32_t now = pit_get_ticks();
    if (needs_composite || (wm_is_dirty() && now - last_composite_tick >= 4)) {
        task_set_current(TASK_WM);
        last_composite_tick = now;
        wm_composite();
    }

    task_set_current(TASK_IDLE);
}

void (*desktop_get_idle_terminal_cb(void))(void) {
    return desktop_idle_terminal;
}

/* ═══ Terminal Window (WM-managed) ═════════════════════════════ */

void desktop_open_terminal(void) {
    int fb_w = (int)gfx_width(), fb_h = (int)gfx_height();

    int tw = fb_w - 80;
    int th = fb_h - DOCK_PILL_H - DOCK_BOTTOM_MARGIN - MENUBAR_H - 20;
    int tx = 40;
    int ty = MENUBAR_H + 4;

    active_terminal_win = wm_create_window(tx, ty, tw, th, "Terminal");

    int pw, ph;
    uint32_t *cvs = wm_get_canvas(active_terminal_win, &pw, &ph);
    terminal_set_canvas(active_terminal_win, cvs, pw, ph);
    terminal_set_window_bg(DT_WIN_BG);
    wm_clear_canvas(active_terminal_win, DT_WIN_BG);
    wm_composite();
}

void desktop_close_terminal(void) {
    terminal_clear_canvas();

    if (active_terminal_win >= 0) {
        wm_destroy_window(active_terminal_win);
        active_terminal_win = -1;
    }

    size_t fb_w = gfx_width(), fb_h = gfx_height();
    terminal_set_window(0, 0, fb_w / FONT_W, fb_h / FONT_H);
    terminal_set_window_bg(0);
}

/* ═══ App Launch / Close (Phase 3) ═════════════════════════════ */

static int dock_index_for_action(int action) {
    for (int i = 0; i < dock_item_count; i++) {
        if (dock_dynamic[i].action == action) return i;
    }
    return -1;
}

static void desktop_launch_app(int action);
static void desktop_close_focused_app(void);

static void desktop_launch_app(int action) {
    int dock_idx = dock_index_for_action(action);

    /* If app already running, focus its window */
    if (dock_idx >= 0) {
        int existing = find_running_app_by_dock(dock_idx);
        if (existing >= 0 && (running_apps[existing].ui_win || running_apps[existing].is_terminal)) {
            wm_focus_window(running_apps[existing].wm_id);
            wm_composite();
            return;
        }
    }

    switch (action) {
    case DESKTOP_ACTION_TERMINAL: {
        /* Non-blocking terminal launch */
        if (active_terminal_win >= 0) {
            wm_focus_window(active_terminal_win);
            wm_composite();
            break;
        }
        desktop_open_terminal();

        /* Register it so indicator dot shows */
        int ti = -1;
        for (int i = 0; i < MAX_RUNNING_APPS; i++) {
            if (!running_apps[i].active) { ti = i; break; }
        }
        if (ti >= 0) {
            running_apps[ti].active = 1;
            running_apps[ti].wm_id = active_terminal_win;
            running_apps[ti].dock_index = dock_idx;
            running_apps[ti].ui_win = 0;
            running_apps[ti].on_event = 0;
            running_apps[ti].on_close = 0;
            running_apps[ti].is_terminal = 1;
            running_apps[ti].task_id = wm_get_task_id(active_terminal_win);
            if (running_apps[ti].task_id >= 0)
                task_set_name(running_apps[ti].task_id, "Terminal");
            rebuild_dock_items();
        }

        terminal_close_pending = 0;
        shell_init_interactive();
        shell_draw_prompt();
        wm_composite();
        break;  /* Return to event loop — no blocking! */
    }

    case DESKTOP_ACTION_EDITOR: {
        ui_window_t *win = app_editor_create();
        if (win) {
            int ri = register_app(win->wm_id, dock_idx, win,
                                  app_editor_on_event, 0, "Editor");
            (void)ri;
            ui_window_redraw(win);
            wm_composite();
        }
        break;
    }

    case DESKTOP_ACTION_FILES: {
        ui_window_t *win = app_filemgr_create();
        if (win) {
            int ri = register_app(win->wm_id, dock_idx, win,
                                  app_filemgr_on_event, app_filemgr_on_close, "Files");
            (void)ri;
            ui_window_redraw(win);
            wm_composite();
        }
        break;
    }

    case DESKTOP_ACTION_BROWSER: {
        ui_window_t *win = app_taskmgr_create();
        if (win) {
            int ri = register_app_ex(win->wm_id, dock_idx, win,
                                     app_taskmgr_on_event, 0,
                                     app_taskmgr_on_tick, 120, "Activity");
            (void)ri;
            ui_window_redraw(win);
            wm_composite();
        }
        break;
    }

    case DESKTOP_ACTION_SETTINGS: {
        ui_window_t *win = app_settings_create();
        if (win) {
            int ri = register_app(win->wm_id, dock_idx, win,
                                  app_settings_on_event, 0, "Settings");
            (void)ri;
            ui_window_redraw(win);
            wm_composite();
        }
        break;
    }

    case DESKTOP_ACTION_TRASH: {
        /* Open file explorer navigated to ~/Trash/ */
        const char *user = user_get_current();
        if (user) {
            char trash_path[128];
            snprintf(trash_path, sizeof(trash_path), "/home/%s/Trash", user);
            /* Ensure Trash dir exists */
            fs_create_file(trash_path, 1);
            fs_change_directory(trash_path);
        }
        ui_window_t *win = app_filemgr_create();
        if (win) {
            int ri = register_app(win->wm_id, dock_idx, win,
                                  app_filemgr_on_event, app_filemgr_on_close, "Trash");
            (void)ri;
            ui_window_redraw(win);
            wm_composite();
        }
        break;
    }

    default:
        break;
    }
}

static void desktop_close_focused_app(void) {
    int fid = wm_get_focused_id();
    if (fid < 0) return;

    int ri = find_running_app_by_wm(fid);
    if (ri < 0) return;

    running_app_t *app = &running_apps[ri];

    if (app->is_terminal) {
        shell_fg_app_t *fg = shell_get_fg_app();
        if (fg && fg->on_close) fg->on_close();
        desktop_close_terminal();
        unregister_app(ri);
        wm_composite();
        return;
    }

    if (app->on_close && app->ui_win)
        app->on_close(app->ui_win);

    if (app->ui_win)
        ui_window_destroy(app->ui_win);

    unregister_app(ri);
    wm_composite();
}

/* ═══ Self-contained desktop_run() — Central Event Loop ════════ */

int desktop_run(void) {
    /* Clear app registry */
    for (int i = 0; i < MAX_RUNNING_APPS; i++)
        running_apps[i].active = 0;

    /* Initialize WM */
    wm_initialize();
    wm_set_bg_draw(desktop_bg_draw);
    wm_set_post_composite(ctx_post_composite);

    int fb_w = (int)gfx_width(), fb_h = (int)gfx_height();
    rebuild_dock_items();

    /* Initialize clock cache */
    get_time_str(last_clock_str);

    /* Load desktop file icons */
    desktop_load_icons();

    /* Initialize event system */
    ui_event_init();

    /* Draw desktop background + menubar + dock */
    draw_gradient(fb_w, fb_h);
    desktop_draw_menubar();
    desktop_draw_icons();
    desktop_draw_dock();

    if (desktop_first_show) {
        desktop_first_show = 0;
        gfx_crossfade(8, 30);

        /* Welcome toast on login */
        const char *user = user_get_current();
        char welcome_msg[80];
        snprintf(welcome_msg, sizeof(welcome_msg), "Welcome back, %s",
                 user ? user : "user");
        toast_show("ImposOS", "Welcome", welcome_msg, TOAST_INFO);
    } else {
        gfx_flip();
    }

    gfx_draw_mouse_cursor(mouse_get_x(), mouse_get_y());
    keyboard_set_idle_callback(desktop_unified_idle);

    /* Main event loop */
    while (1) {
        /* Redraw dirty app windows — credit redraw to the app */
        int needs_composite = 0;
        for (int i = 0; i < MAX_RUNNING_APPS; i++) {
            if (running_apps[i].active && running_apps[i].ui_win &&
                running_apps[i].ui_win->dirty) {
                if (running_apps[i].task_id >= 0)
                    task_set_current(running_apps[i].task_id);
                ui_window_redraw(running_apps[i].ui_win);
                needs_composite = 1;
            }
        }
        if (needs_composite || wm_is_dirty()) {
            task_set_current(TASK_WM);
            wm_composite();
        }

        /* Check for queued events first */
        ui_event_t ev;
        if (ui_event_pending()) {
            ui_poll_event(&ev);
        } else {
            /* Block on getchar (idle callback runs) */
            char c = getchar();

            /* Check queued events from idle callback */
            if (ui_event_pending()) {
                ui_poll_event(&ev);
            } else {
                /* Wrap raw key */
                ev.type = UI_EVENT_KEY_PRESS;
                ev.key.key = c;
                ev.key.ctrl = 0;
                ev.key.alt = 0;
                ev.key.shift = 0;
            }
        }

        /* Handle context menu action (encoded as dock event 100+) */
        if (ev.type == UI_EVENT_DOCK && ev.dock.action >= 100) {
            int menu_idx = ev.dock.action - 100;
            if (menu_idx >= 0 && menu_idx < ctx_menu.item_count) {
                int act = ctx_menu.actions[menu_idx];
                int icon_idx = ctx_menu.target_icon;
                ctx_close();

                switch (act) {
                case CTX_ACT_OPEN:
                    if (icon_idx >= 0) {
                        desktop_launch_app(DESKTOP_ACTION_FILES);
                    } else if (ctx_menu.target_dock >= 0) {
                        int didx = ctx_menu.target_dock;
                        if (didx >= 0 && didx < dock_item_count && dock_dynamic[didx].action > 0)
                            desktop_launch_app(dock_dynamic[didx].action);
                        else if (didx >= 0 && didx < dock_item_count && dock_dynamic[didx].wm_id >= 0)
                            wm_focus_window(dock_dynamic[didx].wm_id);
                    }
                    break;

                case CTX_ACT_CLOSE:
                    if (ctx_menu.target_dock >= 0) {
                        int didx2 = ctx_menu.target_dock;
                        if (didx2 >= 0 && didx2 < dock_item_count &&
                            !dock_dynamic[didx2].is_static && dock_dynamic[didx2].wm_id >= 0) {
                            /* Find and close the running app */
                            int ri2 = find_running_app_by_wm(dock_dynamic[didx2].wm_id);
                            if (ri2 >= 0) {
                                if (running_apps[ri2].is_terminal) {
                                    shell_fg_app_t *fg = shell_get_fg_app();
                                    if (fg && fg->on_close) fg->on_close();
                                    desktop_close_terminal();
                                } else {
                                    if (running_apps[ri2].on_close && running_apps[ri2].ui_win)
                                        running_apps[ri2].on_close(running_apps[ri2].ui_win);
                                    if (running_apps[ri2].ui_win)
                                        ui_window_destroy(running_apps[ri2].ui_win);
                                }
                                unregister_app(ri2);
                            }
                        }
                    }
                    break;

                case CTX_ACT_RENAME:
                    if (icon_idx >= 0 && icon_idx < desktop_icon_count) {
                        ctx_renaming = 1;
                        ctx_rename_icon = icon_idx;
                        strncpy(ctx_rename_buf, desktop_icons[icon_idx].name, MAX_NAME_LEN - 1);
                        ctx_rename_buf[MAX_NAME_LEN - 1] = '\0';
                        ctx_rename_cursor = (int)strlen(ctx_rename_buf);
                    }
                    break;

                case CTX_ACT_TRASH: {
                    if (icon_idx >= 0 && icon_idx < desktop_icon_count) {
                        /* Move file from ~/Desktop/ to ~/Trash/ */
                        const char *user = user_get_current();
                        if (user) {
                            char src[128], trash_dir[128];
                            snprintf(src, sizeof(src), "/home/%s/Desktop", user);
                            snprintf(trash_dir, sizeof(trash_dir), "/home/%s/Trash", user);
                            fs_create_file(trash_dir, 1); /* ensure Trash exists */

                            /* Read file content, write to Trash, delete original */
                            uint32_t saved_cwd = fs_get_cwd_inode();
                            fs_change_directory(src);
                            uint8_t fbuf[4096];
                            size_t fsize = sizeof(fbuf);
                            char *fname = desktop_icons[icon_idx].name;
                            if (desktop_icons[icon_idx].type == INODE_DIR) {
                                /* Just delete empty dirs */
                                fs_delete_file(fname);
                            } else if (fs_read_file(fname, fbuf, &fsize) == 0) {
                                fs_change_directory(trash_dir);
                                fs_create_file(fname, 0);
                                fs_write_file(fname, fbuf, fsize);
                                fs_change_directory(src);
                                fs_delete_file(fname);
                            } else {
                                fs_delete_file(fname);
                            }
                            fs_change_directory_by_inode(saved_cwd);
                        }
                        desktop_load_icons();
                    }
                    break;
                }

                case CTX_ACT_REFRESH:
                    desktop_load_icons();
                    break;

                case CTX_ACT_NEW_FILE: {
                    const char *user = user_get_current();
                    if (user) {
                        char desktop_dir[128];
                        snprintf(desktop_dir, sizeof(desktop_dir), "/home/%s/Desktop", user);
                        uint32_t saved_cwd = fs_get_cwd_inode();
                        fs_change_directory(desktop_dir);
                        /* Find unique name */
                        char fname[MAX_NAME_LEN] = "untitled";
                        int n = 1;
                        while (fs_create_file(fname, 0) != 0 && n < 100) {
                            snprintf(fname, sizeof(fname), "untitled%d", n++);
                        }
                        fs_change_directory_by_inode(saved_cwd);
                        desktop_load_icons();
                    }
                    break;
                }

                case CTX_ACT_NEW_FOLDER: {
                    const char *user = user_get_current();
                    if (user) {
                        char desktop_dir[128];
                        snprintf(desktop_dir, sizeof(desktop_dir), "/home/%s/Desktop", user);
                        uint32_t saved_cwd = fs_get_cwd_inode();
                        fs_change_directory(desktop_dir);
                        char dname[MAX_NAME_LEN] = "New Folder";
                        int n = 1;
                        while (fs_create_file(dname, 1) != 0 && n < 100) {
                            snprintf(dname, sizeof(dname), "Folder%d", n++);
                        }
                        fs_change_directory_by_inode(saved_cwd);
                        desktop_load_icons();
                    }
                    break;
                }

                case CTX_ACT_EMPTY_TRASH: {
                    const char *user = user_get_current();
                    if (user) {
                        char trash_dir[128];
                        snprintf(trash_dir, sizeof(trash_dir), "/home/%s/Trash", user);
                        uint32_t saved_cwd = fs_get_cwd_inode();
                        fs_change_directory(trash_dir);
                        fs_dir_entry_info_t tfiles[32];
                        int tcount = fs_enumerate_directory(tfiles, 32, 0);
                        for (int ti = 0; ti < tcount; ti++)
                            fs_delete_file(tfiles[ti].name);
                        fs_change_directory_by_inode(saved_cwd);
                    }
                    break;
                }

                default:
                    break;
                }

                wm_invalidate_bg();
                wm_composite();
            }
            continue;
        }

        /* Handle dock events */
        if (ev.type == UI_EVENT_DOCK) {
            wm_clear_dock_action();
            int da = ev.dock.action;

            if (da == DESKTOP_ACTION_POWER) {
                acpi_shutdown();
                continue;
            }

            /* Negative dock action = running app focus (encoded as -(idx+1)) */
            if (da < 0) {
                int didx = -(da + 1);
                if (didx >= 0 && didx < dock_item_count && dock_dynamic[didx].wm_id >= 0) {
                    wm_focus_window(dock_dynamic[didx].wm_id);
                    wm_composite();
                }
                continue;
            }

            /* Launch app (or focus existing) */
            desktop_launch_app(da);

            /* After terminal returns, redraw dock */
            draw_gradient(fb_w, fb_h);
            desktop_draw_menubar();
            desktop_draw_dock();
            wm_composite();
            continue;
        }

        /* Handle close events */
        if (ev.type == UI_EVENT_CLOSE) {
            desktop_close_focused_app();
            /* Reload desktop icons (files may have changed) */
            desktop_load_icons();
            /* Redraw dock to update indicator dots */
            draw_gradient(fb_w, fb_h);
            desktop_draw_menubar();
            desktop_draw_icons();
            desktop_draw_dock();
            wm_invalidate_bg();
            wm_composite();
            continue;
        }

        /* Handle inline rename mode */
        if (ctx_renaming && ev.type == UI_EVENT_KEY_PRESS) {
            char c = ev.key.key;
            if (c == KEY_ESCAPE) {
                ctx_renaming = 0;
                ctx_rename_icon = -1;
            } else if (c == '\n' || c == '\r') {
                /* Commit rename */
                if (ctx_rename_icon >= 0 && ctx_rename_icon < desktop_icon_count &&
                    ctx_rename_buf[0] != '\0') {
                    const char *user = user_get_current();
                    if (user) {
                        char desktop_dir[128];
                        snprintf(desktop_dir, sizeof(desktop_dir), "/home/%s/Desktop", user);
                        uint32_t saved_cwd = fs_get_cwd_inode();
                        fs_change_directory(desktop_dir);
                        fs_rename(desktop_icons[ctx_rename_icon].name, ctx_rename_buf);
                        fs_change_directory_by_inode(saved_cwd);
                    }
                }
                ctx_renaming = 0;
                ctx_rename_icon = -1;
                desktop_load_icons();
            } else if (c == '\b') {
                if (ctx_rename_cursor > 0) {
                    ctx_rename_cursor--;
                    /* Shift chars left */
                    int len = (int)strlen(ctx_rename_buf);
                    for (int i = ctx_rename_cursor; i < len; i++)
                        ctx_rename_buf[i] = ctx_rename_buf[i + 1];
                }
            } else if (c >= 32 && c < 127) {
                int len = (int)strlen(ctx_rename_buf);
                if (len < MAX_NAME_LEN - 2 && ctx_rename_cursor < MAX_NAME_LEN - 2) {
                    /* Shift chars right */
                    for (int i = len + 1; i > ctx_rename_cursor; i--)
                        ctx_rename_buf[i] = ctx_rename_buf[i - 1];
                    ctx_rename_buf[ctx_rename_cursor] = c;
                    ctx_rename_cursor++;
                }
            } else if (c == KEY_LEFT && ctx_rename_cursor > 0) {
                ctx_rename_cursor--;
            } else if (c == KEY_RIGHT && ctx_rename_cursor < (int)strlen(ctx_rename_buf)) {
                ctx_rename_cursor++;
            }
            wm_invalidate_bg();
            wm_composite();
            continue;
        }

        /* Handle finder (double-ctrl) */
        if (ev.type == UI_EVENT_KEY_PRESS && ev.key.key == KEY_FINDER) {
            int result = finder_show();
            if (result > 0) {
                desktop_launch_app(result);
            }
            /* Redraw after finder overlay */
            draw_gradient(fb_w, fb_h);
            desktop_draw_menubar();
            desktop_draw_dock();
            wm_composite();
            keyboard_set_idle_callback(desktop_unified_idle);
            continue;
        }

        if (ev.type == UI_EVENT_KEY_PRESS) {
            char c = ev.key.key;

            /* Alt+Tab */
            if (c == KEY_ALT_TAB) {
                wm_cycle_focus();
                continue;
            }

            /* Super key — toggle mobile view */
            if (c == KEY_SUPER) {
                if (desktop_view_mode == VIEW_DESKTOP) {
                    desktop_view_mode = VIEW_MOBILE;
                    mobile_selected = 0;
                    desktop_draw_mobile_view();

                    /* Mobile view event mini-loop */
                    while (desktop_view_mode == VIEW_MOBILE) {
                        char mc = getchar();
                        if (mc == KEY_SUPER || mc == KEY_ESCAPE) {
                            desktop_view_mode = VIEW_DESKTOP;
                            break;
                        }
                        if (mc == KEY_LEFT && mobile_selected > 0) {
                            mobile_selected--;
                            desktop_draw_mobile_view();
                        }
                        if (mc == KEY_RIGHT && mobile_selected < MOBILE_APP_COUNT - 1) {
                            mobile_selected++;
                            desktop_draw_mobile_view();
                        }
                        if (mc == KEY_UP && mobile_selected >= MOBILE_COLS) {
                            mobile_selected -= MOBILE_COLS;
                            desktop_draw_mobile_view();
                        }
                        if (mc == KEY_DOWN && mobile_selected + MOBILE_COLS < MOBILE_APP_COUNT) {
                            mobile_selected += MOBILE_COLS;
                            desktop_draw_mobile_view();
                        }
                        if (mc == '\n') {
                            desktop_view_mode = VIEW_DESKTOP;
                            int action = mobile_actions[mobile_selected];
                            if (action == DESKTOP_ACTION_POWER) {
                                acpi_shutdown();
                            } else {
                                /* Redraw desktop, then launch */
                                wm_invalidate_bg();
                                wm_composite();
                                desktop_launch_app(action);
                            }
                            break;
                        }
                    }
                    /* Restore desktop view */
                    wm_invalidate_bg();
                    wm_composite();
                }
                continue;
            }

            /* Check if any app window is focused */
            int fid = wm_get_focused_id();
            int ri = (fid >= 0) ? find_running_app_by_wm(fid) : -1;

            /* Terminal key dispatch (non-blocking at prompt) */
            if (ri >= 0 && running_apps[ri].is_terminal) {
                task_set_current(TASK_SHELL);

                /* Check for foreground app first */
                shell_fg_app_t *fg = shell_get_fg_app();
                if (fg) {
                    if (fg->task_id >= 0) task_set_current(fg->task_id);
                    fg->on_key(c);
                    wm_composite();
                    task_set_current(TASK_WM);
                    continue;
                }

                if (c == KEY_ESCAPE) {
                    /* Close terminal */
                    desktop_close_terminal();
                    unregister_app(ri);
                    draw_gradient(fb_w, fb_h);
                    desktop_draw_menubar();
                    desktop_draw_dock();
                    wm_composite();
                    task_set_current(TASK_WM);
                    continue;
                }

                int result = shell_handle_key(c);
                wm_composite();  /* Show character echo immediately */

                if (result == 1) {
                    /* Execute command — temporarily blocking */
                    keyboard_set_idle_callback(desktop_idle_terminal);
                    shell_history_add(shell_get_command());
                    config_tick_second();
                    shell_process_command((char *)shell_get_command());
                    keyboard_set_idle_callback(desktop_unified_idle);

                    if (shell_exit_requested || terminal_close_pending) {
                        shell_exit_requested = 0;
                        terminal_close_pending = 0;
                        desktop_close_terminal();
                        unregister_app(ri);
                        draw_gradient(fb_w, fb_h);
                        desktop_draw_menubar();
                        desktop_draw_dock();
                        wm_composite();
                    } else if (shell_get_fg_app()) {
                        /* Non-blocking command running — don't show prompt */
                        wm_composite();
                    } else {
                        shell_draw_prompt();
                        wm_composite();
                    }
                } else if (result == 2) {
                    shell_draw_prompt();
                    wm_composite();
                }
                task_set_current(TASK_WM);
                continue;
            }

            if (ri >= 0 && running_apps[ri].ui_win) {
                /* Set current task for CPU tracking */
                if (running_apps[ri].task_id >= 0)
                    task_set_current(running_apps[ri].task_id);

                /* Escape closes focused app */
                if (c == KEY_ESCAPE) {
                    desktop_close_focused_app();
                    draw_gradient(fb_w, fb_h);
                    desktop_draw_menubar();
                    desktop_draw_dock();
                    wm_composite();
                    continue;
                }

                /* Dispatch to app callback */
                if (running_apps[ri].on_event)
                    running_apps[ri].on_event(running_apps[ri].ui_win, &ev);

                /* Widget dispatch */
                ui_dispatch_event(running_apps[ri].ui_win, &ev);

                if (running_apps[ri].ui_win->dirty) {
                    ui_window_redraw(running_apps[ri].ui_win);
                    wm_composite();
                }
                continue;
            }

            /* Dock is mouse-only — no keyboard navigation */
        }

        /* Mouse events: dispatch to focused app or desktop icons */
        if (ev.type == UI_EVENT_MOUSE_DOWN || ev.type == UI_EVENT_MOUSE_UP) {
            int fid = wm_get_focused_id();
            int ri = (fid >= 0) ? find_running_app_by_wm(fid) : -1;
            if (ri >= 0 && running_apps[ri].ui_win) {
                if (running_apps[ri].task_id >= 0)
                    task_set_current(running_apps[ri].task_id);
                ui_dispatch_event(running_apps[ri].ui_win, &ev);
                if (running_apps[ri].active && running_apps[ri].ui_win->dirty) {
                    ui_window_redraw(running_apps[ri].ui_win);
                    wm_composite();
                }
            } else if (ev.type == UI_EVENT_MOUSE_UP && desktop_icon_count > 0) {
                /* Mouse-up on desktop — check for double-click on icons */
                int mx2 = ev.mouse.x, my2 = ev.mouse.y;
                int hit = desktop_hit_icon(mx2, my2);
                if (hit >= 0 && !dclick_was_drag) {
                    uint32_t now = pit_get_ticks();
                    if (hit == dclick_icon && (now - dclick_tick) <= 20) {
                        /* Double-click within 200ms: open */
                        dclick_icon = -1;
                        if (desktop_icons[hit].type == INODE_DIR) {
                            desktop_launch_app(DESKTOP_ACTION_FILES);
                        } else {
                            desktop_launch_app(DESKTOP_ACTION_EDITOR);
                        }
                        desktop_deselect_all_icons();
                    } else {
                        /* First click: select + record for double-click */
                        dclick_icon = hit;
                        dclick_tick = now;
                        desktop_deselect_all_icons();
                        desktop_icons[hit].selected = 1;
                    }
                } else if (hit < 0 && !dclick_was_drag) {
                    desktop_deselect_all_icons();
                    dclick_icon = -1;
                }
                dclick_was_drag = 0;
                wm_invalidate_bg();
                wm_composite();
            }
        }
    }
}
