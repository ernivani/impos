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
        /* Flip the clean region first */
        gfx_flip_rect(x, y, w, h);
        /* Draw a dark overlay on top with decreasing alpha */
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

#define SLIDE_FROM_LEFT   0
#define SLIDE_FROM_RIGHT  1
#define SLIDE_FROM_TOP    2
#define SLIDE_FROM_BOTTOM 3

static void desktop_slide_in(int x, int y, int w, int h, int direction, int steps, int delay_ms) {
    uint32_t *fb = gfx_framebuffer();
    uint32_t *bb = gfx_backbuffer();
    uint32_t pitch4 = gfx_pitch() / 4;
    int fw = (int)gfx_width(), fh = (int)gfx_height();

    for (int i = 1; i <= steps; i++) {
        int x0 = x < 0 ? 0 : x;
        int y0 = y < 0 ? 0 : y;
        int x1 = x + w; if (x1 > fw) x1 = fw;
        int y1 = y + h; if (y1 > fh) y1 = fh;

        /* Clear region in framebuffer */
        for (int row = y0; row < y1; row++)
            for (int col = x0; col < x1; col++)
                fb[row * pitch4 + col] = 0;

        /* Calculate offset based on direction */
        int ox = 0, oy = 0;
        int progress = i * 256 / steps;
        switch (direction) {
        case SLIDE_FROM_LEFT:   ox = -w + (w * progress / 256); break;
        case SLIDE_FROM_RIGHT:  ox = w - (w * progress / 256);  break;
        case SLIDE_FROM_TOP:    oy = -h + (h * progress / 256); break;
        case SLIDE_FROM_BOTTOM: oy = h - (h * progress / 256);  break;
        }

        /* Copy from backbuffer with offset */
        for (int row = y0; row < y1; row++) {
            int src_y = row - oy;
            if (src_y < y0 || src_y >= y1) continue;
            for (int col = x0; col < x1; col++) {
                int src_x = col - ox;
                if (src_x < x0 || src_x >= x1) continue;
                fb[row * pitch4 + col] = bb[src_y * pitch4 + src_x];
            }
        }
        pit_sleep_ms(delay_ms);
    }
}

/* Format a 2-digit value with leading zero (our snprintf has no %02d) */
static void fmt2(char *dst, int val) {
    dst[0] = '0' + (val / 10) % 10;
    dst[1] = '0' + val % 10;
}

/* ═══ Colored Dock Icons (20x20 area, centered in 34x34 cell) ══ */

/* Each icon uses its own color palette for depth & identity */

static void icon_folder(int x, int y, int sel) {
    /* Warm amber folder */
    uint32_t body = sel ? GFX_RGB(255, 200, 80) : GFX_RGB(220, 170, 55);
    uint32_t tab  = sel ? GFX_RGB(240, 180, 50) : GFX_RGB(190, 140, 40);
    uint32_t dark = GFX_RGB(160, 110, 30);
    uint32_t fold = sel ? GFX_RGB(255, 215, 120) : GFX_RGB(240, 190, 80);

    gfx_fill_rect(x + 2, y + 4, 7, 2, tab);
    gfx_fill_rect(x + 3, y + 7, 15, 10, dark);   /* shadow */
    gfx_fill_rect(x + 2, y + 6, 15, 10, body);
    gfx_fill_rect(x + 2, y + 9, 15, 1, fold);    /* fold line */
    gfx_fill_rect(x + 3, y + 10, 13, 5, dark);   /* inner depth */
}

static void icon_terminal(int x, int y, int sel) {
    /* Dark window with green prompt */
    uint32_t frame = sel ? GFX_RGB(100, 100, 100) : GFX_RGB(70, 70, 70);
    uint32_t inner = GFX_RGB(15, 15, 15);
    uint32_t green = sel ? GFX_RGB(100, 255, 140) : GFX_RGB(80, 220, 120);
    uint32_t title = GFX_RGB(30, 30, 30);

    gfx_draw_rect(x + 2, y + 2, 16, 16, frame);
    gfx_fill_rect(x + 3, y + 3, 14, 3, title);
    /* Traffic light dots */
    gfx_fill_circle(x + 5, y + 4, 1, GFX_RGB(255, 95, 87));
    gfx_fill_circle(x + 8, y + 4, 1, GFX_RGB(255, 189, 46));
    gfx_fill_circle(x + 11, y + 4, 1, GFX_RGB(39, 201, 63));
    /* Content area */
    gfx_fill_rect(x + 3, y + 6, 14, 11, inner);
    /* > prompt */
    gfx_draw_line(x + 5, y + 9, x + 8, y + 12, green);
    gfx_draw_line(x + 5, y + 15, x + 8, y + 12, green);
    /* _ cursor */
    gfx_fill_rect(x + 10, y + 14, 5, 1, green);
}

static void icon_activity(int x, int y, int sel) {
    /* Activity monitor — bar chart */
    uint32_t c1 = sel ? GFX_RGB(100, 200, 255) : GFX_RGB(66, 150, 220);
    uint32_t c2 = sel ? GFX_RGB(80, 220, 140)  : GFX_RGB(60, 180, 110);
    uint32_t c3 = sel ? GFX_RGB(255, 160, 80)  : GFX_RGB(220, 130, 60);
    uint32_t base_c = sel ? GFX_RGB(120, 120, 120) : GFX_RGB(80, 80, 80);

    /* Base line */
    gfx_fill_rect(x + 2, y + 17, 16, 1, base_c);

    /* 4 bars of varying height */
    gfx_fill_rect(x + 3,  y + 10, 3, 7, c1);
    gfx_fill_rect(x + 7,  y + 5,  3, 12, c2);
    gfx_fill_rect(x + 11, y + 8,  3, 9, c3);
    gfx_fill_rect(x + 15, y + 3,  3, 14, c1);
}

static void icon_pencil(int x, int y, int sel) {
    /* Orange pencil */
    uint32_t body = sel ? GFX_RGB(255, 175, 50) : GFX_RGB(230, 150, 30);
    uint32_t dark = GFX_RGB(180, 100, 20);
    uint32_t tip  = GFX_RGB(200, 200, 200);
    uint32_t eras = GFX_RGB(255, 120, 120);

    /* Body diagonal (3 parallel lines for width) */
    gfx_draw_line(x + 14, y + 4, x + 6, y + 14, body);
    gfx_draw_line(x + 13, y + 3, x + 5, y + 13, body);
    gfx_draw_line(x + 15, y + 4, x + 7, y + 14, dark);
    /* Eraser top */
    gfx_fill_rect(x + 13, y + 2, 3, 3, eras);
    /* Tip */
    gfx_put_pixel(x + 4, y + 15, tip);
    gfx_put_pixel(x + 5, y + 14, tip);
    /* Underline */
    gfx_fill_rect(x + 3, y + 17, 8, 1, GFX_RGB(60, 60, 60));
}

static void icon_gear(int x, int y, int sel) {
    /* Silver/steel gear */
    uint32_t main_c = sel ? GFX_RGB(200, 200, 200) : GFX_RGB(150, 150, 150);
    uint32_t dark   = sel ? GFX_RGB(140, 140, 140) : GFX_RGB(100, 100, 100);
    uint32_t center = sel ? GFX_RGB(230, 230, 230) : GFX_RGB(180, 180, 180);
    int cx2 = x + 10, cy2 = y + 10;

    /* Outer ring */
    gfx_circle_ring(cx2, cy2, 6, 1, dark);
    /* 8 teeth — cardinal + diagonal */
    gfx_fill_rect(cx2 - 1, cy2 - 8, 3, 3, main_c);
    gfx_fill_rect(cx2 - 1, cy2 + 5, 3, 3, main_c);
    gfx_fill_rect(cx2 - 8, cy2 - 1, 3, 3, main_c);
    gfx_fill_rect(cx2 + 5, cy2 - 1, 3, 3, main_c);
    gfx_fill_rect(cx2 + 3, cy2 - 6, 3, 3, dark);
    gfx_fill_rect(cx2 - 6, cy2 - 6, 3, 3, dark);
    gfx_fill_rect(cx2 + 3, cy2 + 3, 3, 3, dark);
    gfx_fill_rect(cx2 - 6, cy2 + 3, 3, 3, dark);
    /* Center hole */
    gfx_circle_ring(cx2, cy2, 3, 1, center);
    gfx_fill_circle(cx2, cy2, 1, GFX_RGB(40, 40, 40));
}

static void icon_monitor(int x, int y, int sel) {
    /* Monitor with blue screen */
    uint32_t frame = sel ? GFX_RGB(180, 180, 180) : GFX_RGB(130, 130, 130);
    uint32_t screen = sel ? GFX_RGB(50, 130, 220) : GFX_RGB(35, 100, 180);
    uint32_t stand = sel ? GFX_RGB(140, 140, 140) : GFX_RGB(100, 100, 100);

    gfx_draw_rect(x + 2, y + 3, 16, 11, frame);
    gfx_fill_rect(x + 3, y + 4, 14, 9, screen);
    /* Screen reflection highlight */
    gfx_fill_rect(x + 4, y + 5, 5, 1, GFX_RGB(80, 160, 240));
    /* Stand */
    gfx_fill_rect(x + 8, y + 14, 4, 2, stand);
    gfx_fill_rect(x + 5, y + 16, 10, 1, stand);
}

static void icon_power(int x, int y, int sel) {
    /* Red power icon */
    uint32_t col = sel ? GFX_RGB(255, 100, 100) : GFX_RGB(200, 70, 70);
    int cx2 = x + 10, cy2 = y + 10;

    gfx_circle_ring(cx2, cy2, 7, 1, col);
    /* Gap at top */
    gfx_fill_rect(cx2 - 2, cy2 - 8, 5, 5, 0);
    /* Vertical line through gap */
    gfx_fill_rect(cx2, cy2 - 8, 1, 8, col);
}

/* ═══ Desktop Clock ═════════════════════════════════════════════ */

static void get_time_str(char *buf) {
    datetime_t dt;
    config_get_datetime(&dt);
    fmt2(buf, dt.hour);
    buf[2] = ':';
    fmt2(buf + 3, dt.minute);
    buf[5] = '\0';
}

static void draw_clock(int fb_w, int fb_h) {
    datetime_t dt;
    config_get_datetime(&dt);

    char timebuf[6];
    fmt2(timebuf, dt.hour);
    timebuf[2] = ':';
    fmt2(timebuf + 3, dt.minute);
    timebuf[5] = '\0';

    int scale = 5;
    int tw = gfx_string_scaled_w(timebuf, scale);
    int tx = fb_w - tw - 40;
    int ty = fb_h - TASKBAR_H - 16 * scale - 60;
    gfx_draw_string_scaled(tx, ty, timebuf, DT_TEXT, scale);

    /* Date */
    static const char *months[] = {
        "", "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
    };
    static const char *wdays[] = {
        "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"
    };
    int y2 = dt.year, m2 = dt.month, d2 = dt.day;
    if (m2 < 3) { m2 += 12; y2--; }
    int dow = (d2 + 13 * (m2 + 1) / 5 + y2 + y2 / 4 - y2 / 100 + y2 / 400) % 7;
    dow = (dow + 6) % 7;

    char datebuf[32];
    const char *mn = (dt.month >= 1 && dt.month <= 12) ? months[dt.month] : "???";
    const char *dn = (dow >= 0 && dow <= 6) ? wdays[dow] : "???";
    snprintf(datebuf, sizeof(datebuf), "%s, %s %d", dn, mn, dt.day);

    int dy = ty + 16 * scale + 10;
    int dw = (int)strlen(datebuf) * FONT_W;
    gfx_draw_string_nobg(fb_w - dw - 40, dy, datebuf, DT_TEXT_DIM);

    char infobuf[64];
    snprintf(infobuf, sizeof(infobuf), "%dx%d // %dMB RAM", fb_w, fb_h, (int)gfx_get_system_ram_mb());
    int iw = (int)strlen(infobuf) * FONT_W;
    gfx_draw_string_nobg(fb_w - iw - 40, dy + 18, infobuf, DT_TEXT_DIM);
}

/* ═══ Taskbar ═══════════════════════════════════════════════════ */

#define DOCK_ITEMS    6
#define DOCK_SEP_POS  4   /* separator before index 4 (gear) */

static int dock_sel = 1;

static const char *dock_labels[DOCK_ITEMS] = {
    "Files", "Terminal", "Activity", "Editor",
    "Settings", "Monitor"
};

static const int dock_actions[DOCK_ITEMS] = {
    DESKTOP_ACTION_FILES, DESKTOP_ACTION_TERMINAL, DESKTOP_ACTION_BROWSER,
    DESKTOP_ACTION_EDITOR, DESKTOP_ACTION_SETTINGS, DESKTOP_ACTION_MONITOR
};

typedef void (*icon_fn)(int x, int y, int sel);

static icon_fn dock_icons[DOCK_ITEMS] = {
    icon_folder, icon_terminal, icon_activity, icon_pencil,
    icon_gear, icon_monitor
};

void desktop_draw_dock(void) {
    int fb_w = (int)gfx_width(), fb_h = (int)gfx_height();
    int ty = fb_h - TASKBAR_H;

    gfx_fill_rect(0, ty, fb_w, TASKBAR_H, DT_TASKBAR_BG);
    gfx_fill_rect(0, ty, fb_w, 1, GFX_RGB(25, 25, 25));

    /* Center dock pill */
    int item_w = 34, gap = 2, sep_w = 12;
    int dock_w = DOCK_ITEMS * item_w + (DOCK_ITEMS - 1) * gap + sep_w + 16;
    int dock_x = fb_w / 2 - dock_w / 2;
    int dock_y = ty + 7;
    int dock_h = 34;

    gfx_rounded_rect(dock_x, dock_y, dock_w, dock_h, 8, DT_DOCK_PILL);

    int ix = dock_x + 8;

    for (int i = 0; i < DOCK_ITEMS; i++) {
        if (i == DOCK_SEP_POS) {
            gfx_fill_rect(ix + 3, dock_y + 7, 1, 20, GFX_RGB(30, 30, 30));
            ix += sep_w;
        }

        int hover = wm_get_dock_hover();
        int selected = (i == dock_sel);
        int highlighted = selected || (i == hover);

        /* Selection highlight bg */
        if (highlighted)
            gfx_rounded_rect(ix, dock_y + 2, item_w - 2, 30, 6, DT_SEL_BG);

        /* Center icon (20x20) inside cell (34x34): offset = (34-20)/2 = 7 */
        dock_icons[i](ix + 7, dock_y + 7, highlighted);

        ix += item_w + gap;
    }

    /* Label tooltip above dock: prefer hover, fallback to keyboard selection */
    int label_idx = wm_get_dock_hover();
    if (label_idx < 0) label_idx = dock_sel;
    const char *label = dock_labels[label_idx];
    int lw = (int)strlen(label) * FONT_W;
    gfx_draw_string_nobg(fb_w / 2 - lw / 2, ty - 18, label, DT_TEXT_SUB);

    /* ── Right tray: username + clock + power ── */
    char clk[6];
    get_time_str(clk);

    /* Power icon at far right */
    int pr_x = fb_w - 28;
    int pr_y = ty + 14;
    icon_power(pr_x, pr_y, 0);

    /* Clock text */
    int clk_x = pr_x - (int)strlen(clk) * FONT_W - 14;
    gfx_draw_string_nobg(clk_x, ty + 17, clk, DT_TEXT_MED);

    /* Username */
    const char *user = user_get_current();
    if (!user) user = "user";
    int usr_x = clk_x - (int)strlen(user) * FONT_W - 14;
    gfx_draw_string_nobg(usr_x, ty + 17, user, DT_TEXT_MED);
}

/* ═══ Desktop Draw ══════════════════════════════════════════════ */

void desktop_init(void) {
    gfx_clear(0);
    int fb_w = (int)gfx_width(), fb_h = (int)gfx_height();
    draw_clock(fb_w, fb_h);
    desktop_draw_dock();

    if (desktop_first_show) {
        desktop_first_show = 0;
        desktop_fade_in(0, 0, fb_w, fb_h, 8, 30);
        desktop_slide_in(0, fb_h - TASKBAR_H, fb_w, TASKBAR_H,
                         SLIDE_FROM_BOTTOM, 6, 25);
    } else {
        gfx_flip();
    }
}

void desktop_draw_chrome(void) {
    desktop_draw_dock();
    gfx_flip();
}

/* ═══ Background draw callback for WM ══════════════════════════ */

static void desktop_bg_draw(void) {
    int fb_w = (int)gfx_width(), fb_h = (int)gfx_height();
    draw_clock(fb_w, fb_h);
}

/* ═══ Desktop Event Loop (WM-based) ════════════════════════════ */

static int active_terminal_win = -1;

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

    /* Always recomposite so terminal canvas output is displayed */
    wm_composite();

    /* If close was requested on the terminal window, trigger shell exit */
    if (wm_close_was_requested()) {
        wm_clear_close_request();
        if (active_terminal_win >= 0) {
            keyboard_request_force_exit();
        }
    }
}

int desktop_run(void) {
    dock_sel = 1;

    /* Initialize WM */
    wm_initialize();

    /* Set background draw callback so WM composite redraws clock */
    wm_set_bg_draw(desktop_bg_draw);

    /* Draw desktop background + dock via WM composite */
    gfx_clear(0);
    int fb_w = (int)gfx_width(), fb_h = (int)gfx_height();
    draw_clock(fb_w, fb_h);
    desktop_draw_dock();

    if (desktop_first_show) {
        desktop_first_show = 0;
        desktop_fade_in(0, 0, fb_w, fb_h, 8, 30);
    } else {
        gfx_flip();
    }

    gfx_draw_mouse_cursor(mouse_get_x(), mouse_get_y());

    /* Set idle callback so mouse is processed while getchar blocks */
    keyboard_set_idle_callback(desktop_idle_main);

    /* Desktop event loop: wait for dock click or keyboard */
    while (1) {
        /* Check WM dock actions from mouse (may have been set by idle callback) */
        int da = wm_get_dock_action();
        if (da) {
            wm_clear_dock_action();
            keyboard_set_idle_callback(0);
            gfx_restore_mouse_cursor();
            return da;
        }

        /* Keyboard: getchar will call idle callback for mouse */
        char c = getchar();

        /* After getchar returns, check if dock was clicked */
        da = wm_get_dock_action();
        if (da) {
            wm_clear_dock_action();
            keyboard_set_idle_callback(0);
            gfx_restore_mouse_cursor();
            return da;
        }

        if (c == KEY_LEFT) {
            if (dock_sel > 0) dock_sel--;
            gfx_clear(0); draw_clock(fb_w, fb_h);
            desktop_draw_dock(); gfx_flip();
            gfx_draw_mouse_cursor(mouse_get_x(), mouse_get_y());
            continue;
        }
        if (c == KEY_RIGHT) {
            if (dock_sel < DOCK_ITEMS - 1) dock_sel++;
            gfx_clear(0); draw_clock(fb_w, fb_h);
            desktop_draw_dock(); gfx_flip();
            gfx_draw_mouse_cursor(mouse_get_x(), mouse_get_y());
            continue;
        }
        if (c == '\n') {
            keyboard_set_idle_callback(0);
            gfx_restore_mouse_cursor();
            return dock_actions[dock_sel];
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
        if (c == KEY_SUPER) {
            continue; /* Already on desktop — no-op */
        }
    }
}

/* ═══ Terminal Window (WM-managed) ═════════════════════════════ */

void desktop_open_terminal(void) {
    int fb_w = (int)gfx_width(), fb_h = (int)gfx_height();

    /* Create a WM window for the terminal */
    int tw = fb_w - 80;
    int th = fb_h - TASKBAR_H - 40;
    int tx = 40;
    int ty = 10;

    active_terminal_win = wm_create_window(tx, ty, tw, th, "Terminal");

    /* Get canvas and set terminal to draw into it */
    int pw, ph;
    uint32_t *cvs = wm_get_canvas(active_terminal_win, &pw, &ph);
    terminal_set_canvas(active_terminal_win, cvs, pw, ph);
    terminal_set_window_bg(DT_WIN_BG);

    /* Clear canvas with window background */
    wm_clear_canvas(active_terminal_win, DT_WIN_BG);

    /* Composite: draws window frame + dock + cursor */
    wm_composite();

    /* Set idle callback so mouse works while shell runs */
    keyboard_set_idle_callback(desktop_idle_mouse);
}

void desktop_close_terminal(void) {
    keyboard_set_idle_callback(0);

    /* Disconnect terminal from canvas before destroying window */
    terminal_clear_canvas();

    if (active_terminal_win >= 0) {
        wm_destroy_window(active_terminal_win);
        active_terminal_win = -1;
    }

    size_t fb_w = gfx_width(), fb_h = gfx_height();
    terminal_set_window(0, 0, fb_w / FONT_W, fb_h / FONT_H);
    terminal_set_window_bg(0);
}
