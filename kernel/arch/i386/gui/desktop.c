#include <kernel/desktop.h>
#include <kernel/gfx.h>
#include <kernel/tty.h>
#include <kernel/user.h>
#include <kernel/config.h>
#include <kernel/idt.h>
#include <kernel/io.h>
#include <kernel/mouse.h>
#include <kernel/wm.h>
#include <kernel/ui_theme.h>
#include <kernel/shell.h>
#include <kernel/filemgr.h>
#include <kernel/taskmgr.h>
#include <kernel/settings_app.h>
#include <kernel/monitor_app.h>
#include <string.h>
#include <stdio.h>

static int desktop_first_show = 1;

void desktop_notify_login(void) {
    desktop_first_show = 1;
}

/* ═══ Animation Helpers ═════════════════════════════════════════ */

static void desktop_fade_in(int x, int y, int w, int h, int steps, int delay_ms) {
    for (int i = steps; i >= 0; i--) {
        uint32_t alpha = (uint32_t)(i * 255 / steps);
        gfx_flip_rect(x, y, w, h);
        if (i > 0) {
            uint32_t *fb = gfx_framebuffer();
            uint32_t pitch4 = gfx_pitch() / 4;
            int x0 = x < 0 ? 0 : x;
            int y0 = y < 0 ? 0 : y;
            int x1 = x + w; if (x1 > (int)gfx_width()) x1 = (int)gfx_width();
            int y1 = y + h; if (y1 > (int)gfx_height()) y1 = (int)gfx_height();
            uint32_t inv_a = 255 - alpha;
            for (int row = y0; row < y1; row++) {
                uint32_t *dst = fb + row * pitch4 + x0;
                for (int col = 0; col < x1 - x0; col++) {
                    uint32_t px = dst[col];
                    uint32_t r = ((px >> 16) & 0xFF) * inv_a / 255;
                    uint32_t g = ((px >> 8) & 0xFF) * inv_a / 255;
                    uint32_t b = (px & 0xFF) * inv_a / 255;
                    dst[col] = (r << 16) | (g << 8) | b;
                }
            }
        }
        pit_sleep_ms(delay_ms);
    }
}

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
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (bl < 0) bl = 0; if (bl > 255) bl = 255;
    return GFX_RGB(r, g, bl);
}

static uint32_t grad_tl, grad_tr, grad_bl, grad_br;

static void draw_gradient(int w, int h) {
    grad_tl = GFX_RGB(100, 85, 90);
    grad_tr = GFX_RGB(75, 65, 85);
    grad_bl = GFX_RGB(170, 120, 100);
    grad_br = GFX_RGB(120, 85, 105);

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

#define MENUBAR_H  28

static void desktop_draw_menubar(void) {
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

static int dock_sel = 1;

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

/* Dock geometry — shared with wm.c via desktop.h accessors */
#define DOCK_ITEM_W   48
#define DOCK_ITEM_GAP 4
#define DOCK_SEP_W    14
#define DOCK_PILL_H   56
#define DOCK_PAD      10
#define DOCK_BOTTOM_MARGIN 14
#define DOCK_PILL_R   16

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

void desktop_draw_dock(void) {
    int fb_w = (int)gfx_width(), fb_h = (int)gfx_height();
    compute_dock_layout(fb_w, fb_h);

    /* Frosted glass pill background */
    gfx_rounded_rect_alpha(dock_pill_x, dock_pill_y, dock_pill_w, DOCK_PILL_H,
                           DOCK_PILL_R, GFX_RGB(42, 40, 48), 160);
    gfx_rounded_rect_outline(dock_pill_x, dock_pill_y, dock_pill_w, DOCK_PILL_H,
                             DOCK_PILL_R, GFX_RGB(85, 82, 94));

    int ix = dock_pill_x + DOCK_PAD;

    for (int i = 0; i < DOCK_ITEMS; i++) {
        if (i == DOCK_SEP_POS) {
            /* Vertical separator line */
            gfx_fill_rect(ix + DOCK_SEP_W / 2, dock_pill_y + 10, 1, DOCK_PILL_H - 20,
                          GFX_RGB(85, 82, 94));
            ix += DOCK_SEP_W;
        }

        int hover = wm_get_dock_hover();
        int selected = (i == dock_sel);
        int highlighted = selected || (i == hover);

        /* Hover/selection highlight behind item */
        if (highlighted) {
            gfx_rounded_rect_alpha(ix + 2, dock_pill_y + 4, DOCK_ITEM_W - 4,
                                   DOCK_PILL_H - 8, 8,
                                   GFX_RGB(255, 255, 255), 25);
        }

        /* Center 20x20 icon in 48-wide cell, vertically in top portion */
        int icon_x = ix + (DOCK_ITEM_W - 20) / 2;
        int icon_y = dock_pill_y + 6;
        dock_icons[i](icon_x, icon_y, highlighted);

        /* Text label below icon */
        const char *label = dock_labels[i];
        int lw = (int)strlen(label) * FONT_W;
        int lx = ix + (DOCK_ITEM_W - lw) / 2;
        int ly = dock_pill_y + DOCK_PILL_H - FONT_H - 4;
        uint32_t tcol = highlighted ? GFX_RGB(255, 255, 255) : GFX_RGB(170, 168, 180);
        gfx_draw_string_nobg(lx, ly, label, tcol);

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
        desktop_fade_in(0, 0, fb_w, fb_h, 8, 30);
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

/* ═══ Desktop Event Loop (WM-based, self-contained) ════════════ */

static int active_terminal_win = -1;

/* shell_loop is defined in kernel.c */
extern void shell_loop(void);

/* Idle callback for desktop main loop: process mouse, break getchar on dock click */
static void desktop_idle_main(void) {
    wm_mouse_idle();
    if (wm_get_dock_action()) {
        keyboard_request_force_exit();
    }
}

/* Idle callback: process mouse via WM while shell is running */
static void desktop_idle_mouse(void) {
    wm_mouse_idle();
    wm_composite();

    if (wm_close_was_requested()) {
        wm_clear_close_request();
        if (active_terminal_win >= 0) {
            keyboard_request_force_exit();
        }
    }
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

    keyboard_set_idle_callback(desktop_idle_mouse);
}

void desktop_close_terminal(void) {
    keyboard_set_idle_callback(0);
    terminal_clear_canvas();

    if (active_terminal_win >= 0) {
        wm_destroy_window(active_terminal_win);
        active_terminal_win = -1;
    }

    size_t fb_w = gfx_width(), fb_h = gfx_height();
    terminal_set_window(0, 0, fb_w / FONT_W, fb_h / FONT_H);
    terminal_set_window_bg(0);
}

/* ═══ Self-contained desktop_run() ═════════════════════════════ */

static void desktop_redraw(int fb_w, int fb_h) {
    draw_gradient(fb_w, fb_h);
    desktop_draw_menubar();
    desktop_draw_dock();
    gfx_flip();
    gfx_draw_mouse_cursor(mouse_get_x(), mouse_get_y());
}

static void handle_action(int action) {
    switch (action) {
    case DESKTOP_ACTION_TERMINAL:
        desktop_open_terminal();
        shell_loop();
        desktop_close_terminal();
        break;

    case DESKTOP_ACTION_EDITOR:
        desktop_open_terminal();
        printf("vi: open a file with 'vi <filename>'\n");
        shell_loop();
        desktop_close_terminal();
        break;

    case DESKTOP_ACTION_FILES:
        app_filemgr();
        break;

    case DESKTOP_ACTION_BROWSER:
        app_taskmgr();
        break;

    case DESKTOP_ACTION_SETTINGS:
        app_settings();
        break;

    case DESKTOP_ACTION_MONITOR:
        app_monitor();
        break;

    default:
        break;
    }
}

int desktop_run(void) {
    dock_sel = 1;

    /* Initialize WM */
    wm_initialize();
    wm_set_bg_draw(desktop_bg_draw);

    int fb_w = (int)gfx_width(), fb_h = (int)gfx_height();
    compute_dock_layout(fb_w, fb_h);

    /* Draw desktop background + menubar + dock */
    draw_gradient(fb_w, fb_h);
    desktop_draw_menubar();
    desktop_draw_dock();

    if (desktop_first_show) {
        desktop_first_show = 0;
        desktop_fade_in(0, 0, fb_w, fb_h, 8, 30);
    } else {
        gfx_flip();
    }

    gfx_draw_mouse_cursor(mouse_get_x(), mouse_get_y());
    keyboard_set_idle_callback(desktop_idle_main);

    /* Main event loop */
    while (1) {
        /* Check dock actions from mouse */
        int da = wm_get_dock_action();
        if (da) {
            wm_clear_dock_action();

            if (da == DESKTOP_ACTION_POWER) {
                keyboard_set_idle_callback(0);
                gfx_restore_mouse_cursor();
                return DESKTOP_ACTION_POWER;
            }

            /* Launch app, then re-init desktop */
            keyboard_set_idle_callback(0);
            gfx_restore_mouse_cursor();
            handle_action(da);

            /* Re-init WM + redraw after app returns */
            wm_initialize();
            wm_set_bg_draw(desktop_bg_draw);
            compute_dock_layout(fb_w, fb_h);
            desktop_redraw(fb_w, fb_h);
            keyboard_set_idle_callback(desktop_idle_main);
            continue;
        }

        char c = getchar();

        /* After getchar returns, check if dock was clicked */
        da = wm_get_dock_action();
        if (da) {
            wm_clear_dock_action();

            if (da == DESKTOP_ACTION_POWER) {
                keyboard_set_idle_callback(0);
                gfx_restore_mouse_cursor();
                return DESKTOP_ACTION_POWER;
            }

            keyboard_set_idle_callback(0);
            gfx_restore_mouse_cursor();
            handle_action(da);

            wm_initialize();
            wm_set_bg_draw(desktop_bg_draw);
            compute_dock_layout(fb_w, fb_h);
            desktop_redraw(fb_w, fb_h);
            keyboard_set_idle_callback(desktop_idle_main);
            continue;
        }

        if (c == KEY_LEFT) {
            if (dock_sel > 0) dock_sel--;
            desktop_redraw(fb_w, fb_h);
            continue;
        }
        if (c == KEY_RIGHT) {
            if (dock_sel < DOCK_ITEMS - 1) dock_sel++;
            desktop_redraw(fb_w, fb_h);
            continue;
        }
        if (c == '\n') {
            int act = dock_actions[dock_sel];

            if (act == DESKTOP_ACTION_POWER) {
                keyboard_set_idle_callback(0);
                gfx_restore_mouse_cursor();
                return DESKTOP_ACTION_POWER;
            }

            keyboard_set_idle_callback(0);
            gfx_restore_mouse_cursor();
            handle_action(act);

            wm_initialize();
            wm_set_bg_draw(desktop_bg_draw);
            compute_dock_layout(fb_w, fb_h);
            desktop_redraw(fb_w, fb_h);
            keyboard_set_idle_callback(desktop_idle_main);
            continue;
        }
        if (c == KEY_ESCAPE) {
            keyboard_set_idle_callback(0);
            gfx_restore_mouse_cursor();
            return DESKTOP_ACTION_POWER;
        }
        if (c == KEY_ALT_TAB) {
            wm_cycle_focus();
            continue;
        }
    }
}
