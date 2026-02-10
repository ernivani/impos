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

static void icon_monitor(int x, int y, int sel) {
    uint32_t frame = sel ? GFX_RGB(180, 180, 180) : GFX_RGB(130, 130, 130);
    uint32_t screen = sel ? GFX_RGB(50, 130, 220) : GFX_RGB(35, 100, 180);
    uint32_t stand = sel ? GFX_RGB(140, 140, 140) : GFX_RGB(100, 100, 100);

    gfx_draw_rect(x + 2, y + 3, 16, 11, frame);
    gfx_fill_rect(x + 3, y + 4, 14, 9, screen);
    gfx_fill_rect(x + 4, y + 5, 5, 1, GFX_RGB(80, 160, 240));
    gfx_fill_rect(x + 8, y + 14, 4, 2, stand);
    gfx_fill_rect(x + 5, y + 16, 10, 1, stand);
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
}

/* ═══ Dock ═════════════════════════════════════════════════════ */

#define DOCK_ITEMS    7
#define DOCK_SEP_POS  5   /* separator before index 5 (Settings) */

static const char *dock_labels[DOCK_ITEMS] = {
    "Files", "Terminal", "Activity", "Editor",
    "Power", "Settings", "Monitor"
};

static const int dock_actions[DOCK_ITEMS] = {
    DESKTOP_ACTION_FILES, DESKTOP_ACTION_TERMINAL, DESKTOP_ACTION_BROWSER,
    DESKTOP_ACTION_EDITOR, DESKTOP_ACTION_POWER,
    DESKTOP_ACTION_SETTINGS, DESKTOP_ACTION_MONITOR
};

typedef void (*icon_fn)(int x, int y, int sel);

static icon_fn dock_icons[DOCK_ITEMS] = {
    icon_folder, icon_terminal, icon_activity, icon_pencil,
    icon_power, icon_gear, icon_monitor
};

/* Phase 2: macOS-style dock dimensions (icon-only, no text labels) */
#define DOCK_ITEM_W   44
#define DOCK_ITEM_GAP 6
#define DOCK_SEP_W    14
#define DOCK_PILL_H   48
#define DOCK_PAD      12
#define DOCK_BOTTOM_MARGIN 10
#define DOCK_PILL_R   20

static int dock_pill_x, dock_pill_y, dock_pill_w;

static void compute_dock_layout(int fb_w, int fb_h) {
    dock_pill_w = DOCK_ITEMS * DOCK_ITEM_W + (DOCK_ITEMS - 1) * DOCK_ITEM_GAP + DOCK_SEP_W + DOCK_PAD * 2;
    dock_pill_x = fb_w / 2 - dock_pill_w / 2;
    dock_pill_y = fb_h - DOCK_BOTTOM_MARGIN - DOCK_PILL_H;
}

int desktop_dock_y(void) { return dock_pill_y; }
int desktop_dock_h(void) { return DOCK_PILL_H; }
int desktop_dock_x(void) { return dock_pill_x; }
int desktop_dock_w(void) { return dock_pill_w; }
int desktop_dock_items(void) { return DOCK_ITEMS; }
int desktop_dock_sep_pos(void) { return DOCK_SEP_POS; }

int desktop_dock_item_rect(int idx, int *out_x, int *out_y, int *out_w, int *out_h) {
    if (idx < 0 || idx >= DOCK_ITEMS) return 0;
    int ix = dock_pill_x + DOCK_PAD;
    for (int i = 0; i < idx; i++) {
        if (i == DOCK_SEP_POS) ix += DOCK_SEP_W;
        ix += DOCK_ITEM_W + DOCK_ITEM_GAP;
    }
    if (idx == DOCK_SEP_POS) ix += DOCK_SEP_W;
    if (out_x) *out_x = ix;
    if (out_y) *out_y = dock_pill_y;
    if (out_w) *out_w = DOCK_ITEM_W;
    if (out_h) *out_h = DOCK_PILL_H;
    return 1;
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

static int register_app(int wm_id, int dock_idx, ui_window_t *ui_win,
                        void (*on_event)(ui_window_t *, ui_event_t *),
                        void (*on_close)(ui_window_t *),
                        const char *app_name) {
    for (int i = 0; i < MAX_RUNNING_APPS; i++) {
        if (!running_apps[i].active) {
            running_apps[i].active = 1;
            running_apps[i].wm_id = wm_id;
            running_apps[i].dock_index = dock_idx;
            running_apps[i].ui_win = ui_win;
            running_apps[i].on_event = on_event;
            running_apps[i].on_close = on_close;
            running_apps[i].is_terminal = 0;
            running_apps[i].task_id = wm_get_task_id(wm_id);
            if (running_apps[i].task_id >= 0)
                task_set_name(running_apps[i].task_id, app_name);
            return i;
        }
    }
    return -1;
}

static void unregister_app(int idx) {
    if (idx >= 0 && idx < MAX_RUNNING_APPS) {
        /* Task lifecycle owned by WM — just clear the slot */
        running_apps[idx].active = 0;
    }
}

static int is_dock_app_running(int dock_idx) {
    return find_running_app_by_dock(dock_idx) >= 0;
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

    for (int i = 0; i < DOCK_ITEMS; i++) {
        if (i == DOCK_SEP_POS) {
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
        dock_icons[i](icon_x, icon_y, highlighted);

        /* Running-app indicator dot below icon */
        if (is_dock_app_running(i)) {
            int dot_x = ix + DOCK_ITEM_W / 2;
            int dot_y = dock_pill_y + DOCK_PILL_H - 6;
            gfx_fill_rect(dot_x - 1, dot_y - 1, 3, 3, GFX_RGB(255, 255, 255));
        }

        /* Tooltip on hover (floating label above dock item) */
        if (hover == i) {
            const char *label = dock_labels[i];
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

/* ═══ Background draw callback for WM ══════════════════════════ */

static void desktop_bg_draw(void) {
    int fb_w = (int)gfx_width(), fb_h = (int)gfx_height();
    draw_gradient(fb_w, fb_h);
    desktop_draw_menubar();
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
    if (needs_composite) {
        task_set_current(TASK_WM);
        wm_composite();
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
    for (int i = 0; i < DOCK_ITEMS; i++) {
        if (dock_actions[i] == action) return i;
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
    case DESKTOP_ACTION_TERMINAL:
    case DESKTOP_ACTION_EDITOR: {
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
                task_set_name(running_apps[ti].task_id,
                              action == DESKTOP_ACTION_EDITOR ? "Editor" : "Terminal");
        }

        terminal_close_pending = 0;
        shell_init_interactive();
        if (action == DESKTOP_ACTION_EDITOR)
            printf("vi: open a file with 'vi <filename>'\n");
        shell_draw_prompt();
        wm_composite();
        break;  /* Return to event loop — no blocking! */
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
            int ri = register_app(win->wm_id, dock_idx, win,
                                  app_taskmgr_on_event, 0, "Activity");
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

    case DESKTOP_ACTION_MONITOR: {
        ui_window_t *win = app_monitor_create();
        if (win) {
            int ri = register_app(win->wm_id, dock_idx, win,
                                  app_monitor_on_event, 0, "Monitor");
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

    int fb_w = (int)gfx_width(), fb_h = (int)gfx_height();
    compute_dock_layout(fb_w, fb_h);

    /* Initialize clock cache */
    get_time_str(last_clock_str);

    /* Initialize event system */
    ui_event_init();

    /* Draw desktop background + menubar + dock */
    draw_gradient(fb_w, fb_h);
    desktop_draw_menubar();
    desktop_draw_dock();

    if (desktop_first_show) {
        desktop_first_show = 0;
        gfx_crossfade(8, 30);
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
        if (needs_composite) {
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

        /* Handle dock events */
        if (ev.type == UI_EVENT_DOCK) {
            wm_clear_dock_action();
            int da = ev.dock.action;

            if (da == DESKTOP_ACTION_POWER) {
                acpi_shutdown();
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
            /* Redraw dock to update indicator dots */
            draw_gradient(fb_w, fb_h);
            desktop_draw_menubar();
            desktop_draw_dock();
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

            /* Super key — no action on desktop */
            if (c == KEY_SUPER)
                continue;

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

        /* Mouse events: dispatch to focused app */
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
            }
        }
    }
}
