/* monitor.c — System Monitor: CPU, memory, and system stats.
 *
 * Shows real-time system information with visual bars.
 * Auto-refreshes every ~1 second.
 * Pattern follows settings.c: singleton window, per-frame tick.
 */
#include <kernel/monitor_app.h>
#include <kernel/ui_window.h>
#include <kernel/gfx.h>
#include <kernel/task.h>
#include <kernel/idt.h>
#include <kernel/pmm.h>
#include <kernel/compositor.h>
#include <kernel/fs.h>
#include <string.h>
#include <stdio.h>

/* ── Layout ─────────────────────────────────────────────────────── */
#define WIN_W       500
#define WIN_H       380
#define SECTION_H    16
#define BAR_H        14
#define BAR_W       300
#define MARGIN       16
#define COL_BG      0xFF1E1E2E
#define COL_PANEL   0xFF181825
#define COL_BORDER  0xFF313244
#define COL_TEXT    0xFFCDD6F4
#define COL_DIM     0xFF6C7086
#define COL_ACCENT  0xFF89B4FA
#define COL_GREEN   0xFFA6E3A1
#define COL_YELLOW  0xFFF9E2AF
#define COL_RED     0xFFF38BA8
#define COL_TEAL    0xFF94E2D5
#define COL_BAR_BG  0xFF313244

/* ── State ──────────────────────────────────────────────────────── */
static int mon_win_id = -1;
static uint32_t mon_last_refresh = 0;

/* ── Drawing helpers ────────────────────────────────────────────── */

static void draw_bar(gfx_surface_t *gs, int x, int y, int w, int h,
                     int pct, uint32_t fill_col) {
    gfx_surf_fill_rect(gs, x, y, w, h, COL_BAR_BG);
    if (pct > 100) pct = 100;
    if (pct > 0) {
        int fw = w * pct / 100;
        if (fw < 1) fw = 1;
        gfx_surf_fill_rect(gs, x, y, fw, h, fill_col);
    }
}

static void draw_section(gfx_surface_t *gs, int *y, const char *title,
                          const char *value, int pct, uint32_t bar_col) {
    gfx_surf_draw_string_smooth(gs, MARGIN, *y, title, COL_DIM, 1);
    gfx_surf_draw_string_smooth(gs, MARGIN + 120, *y, value, COL_TEXT, 1);
    *y += SECTION_H + 2;
    draw_bar(gs, MARGIN, *y, BAR_W, BAR_H, pct, bar_col);
    *y += BAR_H + 12;
}

/* ── Paint ──────────────────────────────────────────────────────── */

static void mon_paint(void) {
    if (mon_win_id < 0) return;
    int cw, ch;
    uint32_t *canvas = ui_window_canvas(mon_win_id, &cw, &ch);
    if (!canvas) return;

    gfx_surface_t gs = { canvas, cw, ch, cw };
    gfx_surf_fill_rect(&gs, 0, 0, cw, ch, COL_BG);

    int y = MARGIN;

    /* Title */
    gfx_surf_draw_string_smooth(&gs, MARGIN, y, "System Monitor", COL_TEXT, 1);
    y += 24;
    gfx_surf_fill_rect(&gs, MARGIN, y, cw - MARGIN * 2, 1, COL_BORDER);
    y += 8;

    /* CPU — estimate total from tasks */
    {
        int total_cpu_x10 = 0;
        for (int i = 0; i < TASK_MAX; i++) {
            task_info_t *t = task_get(i);
            if (!t) continue;
            if (t->sample_total)
                total_cpu_x10 += (int)(t->prev_ticks * 1000 / t->sample_total);
        }
        if (total_cpu_x10 > 1000) total_cpu_x10 = 1000;
        char buf[32];
        snprintf(buf, sizeof(buf), "%d.%d%%", total_cpu_x10 / 10, total_cpu_x10 % 10);
        uint32_t col = total_cpu_x10 > 800 ? COL_RED :
                       total_cpu_x10 > 500 ? COL_YELLOW : COL_GREEN;
        draw_section(&gs, &y, "CPU Usage", buf, total_cpu_x10 / 10, col);
    }

    /* Memory */
    {
        uint32_t free_frames = pmm_free_frame_count();
        uint32_t total_frames = 65536; /* 256MB / 4KB */
        uint32_t used_frames = total_frames - free_frames;
        int pct = (int)(used_frames * 100 / total_frames);
        uint32_t used_mb = (used_frames * 4) / 1024;
        uint32_t total_mb = (total_frames * 4) / 1024;
        char buf[32];
        snprintf(buf, sizeof(buf), "%d / %d MB", (int)used_mb, (int)total_mb);
        uint32_t col = pct > 85 ? COL_RED : pct > 60 ? COL_YELLOW : COL_TEAL;
        draw_section(&gs, &y, "Memory", buf, pct, col);
    }

    /* Disk */
    {
        /* NUM_BLOCKS=8192, each 4KB = 32MB total */
        char buf[32];
        snprintf(buf, sizeof(buf), "%d blocks x 4KB", NUM_BLOCKS);
        draw_section(&gs, &y, "Disk", buf, 30, COL_ACCENT);
    }

    /* Tasks */
    {
        int tc = task_count();
        char buf[32];
        snprintf(buf, sizeof(buf), "%d active", tc);
        int pct = tc * 100 / TASK_MAX;
        draw_section(&gs, &y, "Tasks", buf, pct, COL_GREEN);
    }

    /* FPS */
    {
        uint32_t fps = compositor_get_fps();
        char buf[32];
        snprintf(buf, sizeof(buf), "%d FPS", (int)fps);
        gfx_surf_draw_string_smooth(&gs, MARGIN, y, "Compositor", COL_DIM, 1);
        gfx_surf_draw_string_smooth(&gs, MARGIN + 120, y, buf, COL_ACCENT, 1);
        y += SECTION_H + 4;
    }

    /* Uptime */
    {
        uint32_t ticks = pit_get_ticks();
        uint32_t secs = ticks / 120;
        uint32_t mins = secs / 60;
        uint32_t hrs = mins / 60;
        char buf[32];
        snprintf(buf, sizeof(buf), "%d:%02d:%02d",
                 (int)hrs, (int)(mins % 60), (int)(secs % 60));
        gfx_surf_draw_string_smooth(&gs, MARGIN, y, "Uptime", COL_DIM, 1);
        gfx_surf_draw_string_smooth(&gs, MARGIN + 120, y, buf, COL_TEXT, 1);
    }

    ui_window_damage_all(mon_win_id);
}

/* ── Public API ─────────────────────────────────────────────────── */

void app_monitor_open(void) {
    if (mon_win_id >= 0) {
        ui_window_raise(mon_win_id);
        ui_window_focus(mon_win_id);
        return;
    }
    int sw = (int)gfx_width(), sh = (int)gfx_height();
    mon_win_id = ui_window_create((sw - WIN_W) / 2, (sh - WIN_H) / 2,
                                   WIN_W, WIN_H, "System Monitor");
    mon_last_refresh = 0;
    mon_paint();
}

int monitor_tick(int mx, int my, int btn_down, int btn_up) {
    if (mon_win_id < 0) return 0;

    if (ui_window_close_requested(mon_win_id)) {
        ui_window_close_clear(mon_win_id);
        ui_window_close_animated(mon_win_id);
        mon_win_id = -1;
        return 0;
    }

    /* Auto-refresh every ~1 second */
    uint32_t now = pit_get_ticks();
    if (now - mon_last_refresh >= 120) {
        mon_last_refresh = now;
        mon_paint();
    }

    ui_win_info_t info = ui_window_info(mon_win_id);
    if (info.w <= 0) return 0;
    int lx = mx - info.cx, ly = my - info.cy;
    if (lx >= 0 && ly >= 0 && lx < info.cw && ly < info.ch) {
        if (btn_down) {
            ui_window_focus(mon_win_id);
            ui_window_raise(mon_win_id);
            return 1;
        }
        if (btn_up) return 1;
    }
    return 0;
}

int monitor_win_open(void) { return mon_win_id >= 0; }

/* Legacy stubs */
void app_editor(void) { app_monitor_open(); }
ui_window_t *app_editor_create(void) { return 0; }
void app_editor_on_event(ui_window_t *w, ui_event_t *e) { (void)w; (void)e; }
