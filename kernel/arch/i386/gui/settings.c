/* settings.c — Phase 7: Settings application.
 *
 * Sidebar (170px): Wallpaper | Appearance | Display | About
 * Content pane: wallpaper picker grid + theme dots, or placeholder.
 *
 * Opened via app_launch("settings") or context menu.
 * Can be directed to a specific tab: app_settings_open_to("wallpaper").
 */
#include <kernel/settings_app.h>
#include <kernel/ui_window.h>
#include <kernel/wallpaper.h>
#include <kernel/gfx.h>
#include <kernel/ui_theme.h>
#include <string.h>

/* ── Layout ─────────────────────────────────────────────────────── */
#define WIN_W      680
#define WIN_H      440
#define SIDEBAR_W  170
#define TAB_H       40
#define THUMB_W    140
#define THUMB_H     87    /* 16:10 ratio */
#define DOT_R        9    /* theme dot radius */
#define DOT_GAP     24

/* ── Tab IDs ─────────────────────────────────────────────────────── */
#define TAB_WALLPAPER  0
#define TAB_APPEARANCE 1
#define TAB_DISPLAY    2
#define TAB_ABOUT      3
#define TAB_COUNT      4

static const char *tab_names[TAB_COUNT] = {
    "Wallpaper", "Appearance", "Display", "About"
};

static const uint32_t tab_icon_colors[TAB_COUNT] = {
    0xFF3478F6,   /* Wallpaper  — blue   */
    0xFFCBA6F7,   /* Appearance — purple */
    0xFF94E2D5,   /* Display    — teal   */
    0xFF6C7086,   /* About      — gray   */
};

/* ── State ──────────────────────────────────────────────────────── */
static int settings_win_id = -1;
static int active_tab = TAB_WALLPAPER;

/* Hover tracking */
static int hover_style = -1;
static int hover_dot   = -1;

/* ── Drawing helpers ────────────────────────────────────────────── */

static void fill_rect(uint32_t *px, int pw, int x, int y,
                       int w, int h, uint32_t color) {
    for (int row = y; row < y + h; row++) {
        if (row < 0 || row >= WIN_H) continue;
        for (int col = x; col < x + w; col++) {
            if (col < 0 || col >= WIN_W) continue;
            px[row * pw + col] = color;
        }
    }
}

static void draw_rrect(uint32_t *px, int pw, int x, int y,
                        int w, int h, int r, uint32_t color) {
    for (int row = y; row < y + h; row++) {
        if (row < 0 || row >= WIN_H) continue;
        for (int col = x; col < x + w; col++) {
            if (col < 0 || col >= WIN_W) continue;
            int dx = 0, dy = 0, inside = 1;
            if (col<x+r && row<y+r) { dx=col-(x+r); dy=row-(y+r); }
            else if (col>=x+w-r && row<y+r) { dx=col-(x+w-r-1); dy=row-(y+r); }
            else if (col<x+r && row>=y+h-r) { dx=col-(x+r); dy=row-(y+h-r-1); }
            else if (col>=x+w-r && row>=y+h-r) { dx=col-(x+w-r-1); dy=row-(y+h-r-1); }
            if (dx||dy) inside=(dx*dx+dy*dy<=r*r);
            if (inside) px[row*pw+col] = color;
        }
    }
}

static void draw_circle(uint32_t *px, int pw, int cx_, int cy_,
                         int r, uint32_t color) {
    for (int dy = -r; dy <= r; dy++) {
        int row = cy_ + dy;
        if (row < 0 || row >= WIN_H) continue;
        for (int dx = -r; dx <= r; dx++) {
            int col = cx_ + dx;
            if (col < 0 || col >= WIN_W) continue;
            if (dx*dx + dy*dy <= r*r)
                px[row * pw + col] = color;
        }
    }
}

static int strlenx(const char *s) { int n = 0; while (*s++) n++; return n; }

/* ── Paint the full settings window ─────────────────────────────── */

static void settings_paint(void) {
    if (settings_win_id < 0) return;

    int cw, ch;
    uint32_t *canvas = ui_window_canvas(settings_win_id, &cw, &ch);
    if (!canvas) return;

    gfx_surface_t gs;
    gs.buf = canvas; gs.w = cw; gs.h = ch; gs.pitch = cw;

    /* Window background */
    fill_rect(canvas, cw, 0, 0, cw, ch, 0xFF1E1E2E);

    /* ── Sidebar ────────────────────────────────────────────────── */
    fill_rect(canvas, cw, 0, 0, SIDEBAR_W, ch, 0xFF181825);
    /* Right border */
    fill_rect(canvas, cw, SIDEBAR_W - 1, 0, 1, ch, 0xFF313244);

    for (int t = 0; t < TAB_COUNT; t++) {
        int ty = t * TAB_H + 8;
        int active = (t == active_tab);

        /* Active highlight */
        if (active) {
            draw_rrect(canvas, cw, 6, ty, SIDEBAR_W - 12, TAB_H - 4,
                       6, 0x4D3478F6);
        }

        /* Icon square (8x8 rounded) */
        draw_rrect(canvas, cw, 14, ty + (TAB_H - 4 - 10) / 2,
                   10, 10, 2, tab_icon_colors[t]);

        uint32_t fg = active ? 0xFFFFFFFF : 0xFFA6ADC8;
        gfx_surf_draw_string_smooth(&gs, 30, ty + (TAB_H - 16) / 2,
                             tab_names[t], fg, 1);
    }

    /* ── Content pane ───────────────────────────────────────────── */
    int cx = SIDEBAR_W + 20;
    int cy = 16;

    switch (active_tab) {

    case TAB_WALLPAPER: {
        /* Title */
        gfx_surf_draw_string_smooth(&gs, cx, cy, "Wallpaper",
                             0xFFCDD6F4, 1);
        cy += 24;

        /* Thumbnail grid: 5 style cards */
        int cur_style = wallpaper_get_style();
        for (int s = 0; s < WALLPAPER_STYLE_COUNT; s++) {
            int tx = cx + s * (THUMB_W + 12);
            int ty = cy;

            /* Thumbnail background */
            uint32_t thumb_buf[THUMB_W * THUMB_H];
            wallpaper_draw_thumbnail(thumb_buf, THUMB_W, THUMB_H, s,
                                     wallpaper_get_theme());

            /* Blit thumbnail into canvas */
            for (int row = 0; row < THUMB_H; row++) {
                if (ty + row >= ch) break;
                for (int col = 0; col < THUMB_W; col++) {
                    if (tx + col >= cw) break;
                    canvas[(ty + row) * cw + (tx + col)] =
                        thumb_buf[row * THUMB_W + col];
                }
            }

            /* Active border */
            uint32_t border = (s == cur_style)  ? 0xFF3478F6 :
                              (s == hover_style) ? 0x80FFFFFF : 0x28FFFFFF;
            /* Top/bottom */
            for (int col = tx - 2; col < tx + THUMB_W + 2; col++) {
                if (col < 0 || col >= cw) continue;
                if (ty - 2 >= 0) canvas[(ty-2)*cw+col] = border;
                if (ty + THUMB_H + 1 < ch) canvas[(ty+THUMB_H+1)*cw+col] = border;
            }
            /* Left/right */
            for (int row = ty; row < ty + THUMB_H; row++) {
                if (row < 0 || row >= ch) continue;
                if (tx - 2 >= 0) canvas[row*cw + (tx-2)] = border;
                if (tx + THUMB_W + 1 < cw) canvas[row*cw + (tx+THUMB_W+1)] = border;
            }

            /* Style name below thumbnail */
            const char *sname = wallpaper_style_name(s);
            int nlen = strlenx(sname);
            int nx = tx + (THUMB_W - nlen * 8) / 2;
            uint32_t nfg = (s == cur_style) ? 0xFFCDD6F4 : 0xFF6C7086;
            gfx_surf_draw_string_smooth(&gs, nx, ty + THUMB_H + 6, sname, nfg, 1);
        }
        cy += THUMB_H + 24;

        /* Theme dots */
        {
            int cur_theme = wallpaper_get_theme();
            int tc = wallpaper_theme_count(cur_style);
            gfx_surf_draw_string_smooth(&gs, cx, cy, "Theme:", 0xFF6C7086, 1);
            int dot_x = cx + 60;
            for (int i = 0; i < tc; i++) {
                uint32_t dot_col = wallpaper_theme_color(cur_style, i);
                draw_circle(canvas, cw, dot_x + i * DOT_GAP + DOT_R,
                            cy + DOT_R, DOT_R, dot_col);
                /* Active dot: white ring */
                if (i == cur_theme || i == hover_dot) {
                    uint32_t ring = (i == cur_theme) ? 0xFFFFFFFF : 0x80FFFFFF;
                    int r_out = DOT_R + 2, r_in = DOT_R;
                    int cx_ = dot_x + i * DOT_GAP + DOT_R, cy_ = cy + DOT_R;
                    for (int dy = -r_out; dy <= r_out; dy++) {
                        for (int dx = -r_out; dx <= r_out; dx++) {
                            int d2 = dx*dx+dy*dy;
                            if (d2>=r_in*r_in && d2<=r_out*r_out &&
                                cx_+dx>=0 && cx_+dx<cw &&
                                cy_+dy>=0 && cy_+dy<ch)
                                canvas[(cy_+dy)*cw+(cx_+dx)] = ring;
                        }
                    }
                }
            }

            /* Theme name */
            cy += DOT_R * 2 + 10;
            gfx_surf_draw_string_smooth(&gs, cx, cy,
                                 wallpaper_theme_name(cur_style, cur_theme),
                                 0xFFCDD6F4, 1);
        }
        break;
    }

    case TAB_ABOUT: {
        /* ImposOS logo text */
        gfx_surf_draw_string_smooth(&gs, cx, cy + 20, "ImposOS",
                             0xFFCDD6F4, 1);
        gfx_surf_draw_string_smooth(&gs, cx, cy + 44, "Version 0.1",
                             0xFF89B4FA, 1);
        gfx_surf_draw_string_smooth(&gs, cx, cy + 64,
                             "A concept desktop environment",
                             0xFF45475A, 1);
        gfx_surf_draw_string_smooth(&gs, cx, cy + 88,
                             "Running on bare-metal i386",
                             0xFF45475A, 1);
        gfx_surf_draw_string_smooth(&gs, cx, cy + 112,
                             "No MMU process isolation",
                             0xFF313244, 1);
        gfx_surf_draw_string_smooth(&gs, cx, cy + 128,
                             "All CPU rendering, no GPU",
                             0xFF313244, 1);
        break;
    }

    default: {
        gfx_surf_draw_string_smooth(&gs, cx, cy + 20,
                             "Coming soon...", 0xFF45475A, 1);
        break;
    }
    }

    ui_window_damage_all(settings_win_id);
}

/* ── Hit-testing for wallpaper tab ──────────────────────────────── */

static int settings_mouse_wallpaper(int mx, int my, int btn_up) {
    int cx = SIDEBAR_W + 20;
    int cy = 16 + 24; /* after title */

    int new_hover_style = -1;
    int new_hover_dot   = -1;

    /* Check thumbnails */
    for (int s = 0; s < WALLPAPER_STYLE_COUNT; s++) {
        int tx = cx + s * (THUMB_W + 12);
        if (mx >= tx && mx < tx + THUMB_W &&
            my >= cy && my < cy + THUMB_H) {
            new_hover_style = s;
            if (btn_up) {
                wallpaper_set_style(s, 0);
                settings_paint();
                return 1;
            }
            break;
        }
    }
    cy += THUMB_H + 24;

    /* Check theme dots */
    int cur_style = wallpaper_get_style();
    int tc = wallpaper_theme_count(cur_style);
    int dot_x = cx + 60;
    for (int i = 0; i < tc; i++) {
        int dcx = dot_x + i * DOT_GAP + DOT_R;
        int dcy = cy + DOT_R;
        int dx = mx - dcx, dy = my - dcy;
        if (dx*dx + dy*dy <= (DOT_R + 4)*(DOT_R + 4)) {
            new_hover_dot = i;
            if (btn_up) {
                wallpaper_set_theme(i);
                settings_paint();
                return 1;
            }
            break;
        }
    }

    if (new_hover_style != hover_style || new_hover_dot != hover_dot) {
        hover_style = new_hover_style;
        hover_dot   = new_hover_dot;
        settings_paint();
    }
    return 0;
}

static int settings_mouse_sidebar(int mx, int my, int btn_up) {
    if (mx >= SIDEBAR_W) return 0;
    for (int t = 0; t < TAB_COUNT; t++) {
        int ty = t * TAB_H + 8;
        if (my >= ty && my < ty + TAB_H) {
            if (btn_up && t != active_tab) {
                active_tab = t;
                hover_style = -1;
                hover_dot   = -1;
                settings_paint();
            }
            return 1;
        }
    }
    return 0;
}

/* ── Window event handler ───────────────────────────────────────── */

static void settings_handle_mouse(int mx, int my, int btn_up) {
    settings_mouse_sidebar(mx, my, btn_up);
    if (active_tab == TAB_WALLPAPER && mx >= SIDEBAR_W)
        settings_mouse_wallpaper(mx, my, btn_up);
}

/* ── Public API ─────────────────────────────────────────────────── */

/* Open (or bring to front) the settings window at a specific tab. */
void app_settings_open_to(const char *tab) {
    /* Determine tab index */
    int tid = TAB_WALLPAPER;
    if (tab) {
        if (tab[0]=='d' && tab[1]=='i') tid = TAB_DISPLAY;
        else if (tab[0]=='a' && tab[1]=='b') tid = TAB_ABOUT;
        else if (tab[0]=='a' && tab[1]=='p') tid = TAB_APPEARANCE;
    }

    if (settings_win_id >= 0) {
        /* Already open: bring to front */
        active_tab = tid;
        ui_window_raise(settings_win_id);
        settings_paint();
        return;
    }

    /* Create window centered */
    int sw = (int)gfx_width(), sh = (int)gfx_height();
    int wx = (sw - WIN_W) / 2;
    int wy = (sh - WIN_H) / 2;
    settings_win_id = ui_window_create(wx, wy, WIN_W, WIN_H, "Settings");
    active_tab = tid;
    hover_style = -1; hover_dot = -1;
    settings_paint();
}

/* Frame-loop: check for close request and mouse events.
   Call from desktop_run() while settings window is open.
   Returns 1 if the click was consumed (in content area). */
int settings_tick(int mx, int my, int btn_down, int btn_up) {
    if (settings_win_id < 0) return 0;

    if (ui_window_close_requested(settings_win_id)) {
        ui_window_close_clear(settings_win_id);
        ui_window_close_animated(settings_win_id);
        settings_win_id = -1;
        return 0;
    }

    ui_win_info_t info = ui_window_info(settings_win_id);
    if (info.w <= 0) return 0;

    /* Convert to content-local coords */
    int lx = mx - info.cx;
    int ly = my - info.cy;

    /* Route mouse to content area for hover + click handling */
    if (lx >= 0 && ly >= 0 && lx < info.cw && ly < info.ch) {
        settings_handle_mouse(lx, ly, btn_up);

        /* Consume button events: focus window and prevent WM2 double-handling */
        if (btn_down) {
            ui_window_focus(settings_win_id);
            ui_window_raise(settings_win_id);
            return 1;
        }
        if (btn_up) return 1;
    }

    return 0;
}

/* Legacy stubs for old API */
void app_settings(void) { app_settings_open_to("wallpaper"); }
ui_window_t *app_settings_create(void) { return 0; }
void app_settings_on_event(ui_window_t *w, ui_event_t *e) {
    (void)w; (void)e;
}

int settings_win_open(void) { return settings_win_id >= 0; }
