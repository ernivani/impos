/* taskmgr.c — Task Manager: shows running tasks with CPU/memory.
 *
 * Columns: PID, Name, State, CPU%, Memory
 * Auto-refreshes every ~1 second.
 * Pattern follows settings.c: singleton window, per-frame tick.
 */
#include <kernel/taskmgr.h>
#include <kernel/ui_window.h>
#include <kernel/gfx.h>
#include <kernel/task.h>
#include <kernel/idt.h>
#include <kernel/pmm.h>
#include <string.h>
#include <stdio.h>

/* ── Layout ─────────────────────────────────────────────────────── */
#define WIN_W       620
#define WIN_H       420
#define HDR_H        32
#define ROW_H        20
#define COL_BG      0xFF1E1E2E
#define COL_HDR     0xFF181825
#define COL_BORDER  0xFF313244
#define COL_TEXT    0xFFCDD6F4
#define COL_DIM     0xFF6C7086
#define COL_ACCENT  0xFF89B4FA
#define COL_GREEN   0xFFA6E3A1
#define COL_RED     0xFFF38BA8
#define COL_YELLOW  0xFFF9E2AF

/* ── State ──────────────────────────────────────────────────────── */
static int tm_win_id = -1;
static uint32_t tm_last_refresh = 0;

/* ── Paint ──────────────────────────────────────────────────────── */

static void tm_paint(void) {
    if (tm_win_id < 0) return;
    int cw, ch;
    uint32_t *canvas = ui_window_canvas(tm_win_id, &cw, &ch);
    if (!canvas) return;

    gfx_surface_t gs = { canvas, cw, ch, cw };
    gfx_surf_fill_rect(&gs, 0, 0, cw, ch, COL_BG);

    /* Header bar */
    gfx_surf_fill_rect(&gs, 0, 0, cw, HDR_H, COL_HDR);
    gfx_surf_fill_rect(&gs, 0, HDR_H - 1, cw, 1, COL_BORDER);

    /* Summary line */
    int tc = task_count();
    uint32_t free_frames = pmm_free_frame_count();
    uint32_t free_mb = (free_frames * 4) / 1024;
    char summary[80];
    snprintf(summary, sizeof(summary), "Tasks: %d   Free RAM: %d MB", tc, (int)free_mb);
    gfx_surf_draw_string_smooth(&gs, 10, (HDR_H - 16) / 2, summary, COL_TEXT, 1);

    /* Column headers */
    int y = HDR_H + 6;
    gfx_surf_draw_string_smooth(&gs, 10,  y, "PID",   COL_DIM, 1);
    gfx_surf_draw_string_smooth(&gs, 58,  y, "Name",  COL_DIM, 1);
    gfx_surf_draw_string_smooth(&gs, 260, y, "State", COL_DIM, 1);
    gfx_surf_draw_string_smooth(&gs, 340, y, "CPU%",  COL_DIM, 1);
    gfx_surf_draw_string_smooth(&gs, 410, y, "Mem",   COL_DIM, 1);
    gfx_surf_draw_string_smooth(&gs, 490, y, "Type",  COL_DIM, 1);
    y += ROW_H + 2;
    gfx_surf_fill_rect(&gs, 8, y - 4, cw - 16, 1, COL_BORDER);

    /* Task rows */
    for (int i = 0; i < TASK_MAX; i++) {
        task_info_t *t = task_get(i);
        if (!t) continue;
        if (y + ROW_H > ch - 4) break;

        /* PID */
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", task_get_pid(i));
        gfx_surf_draw_string_smooth(&gs, 10, y, buf, COL_ACCENT, 1);

        /* Name */
        gfx_surf_draw_string_smooth(&gs, 58, y, t->name, COL_TEXT, 1);

        /* State */
        const char *sname;
        uint32_t state_col;
        if (t->killed)                          { sname = "Killed";  state_col = COL_RED; }
        else if (t->state == TASK_STATE_SLEEPING) { sname = "Sleep";   state_col = COL_YELLOW; }
        else if (t->state == TASK_STATE_BLOCKED)  { sname = "Blocked"; state_col = COL_YELLOW; }
        else if (t->state == TASK_STATE_RUNNING)  { sname = "Running"; state_col = COL_GREEN; }
        else                                      { sname = "Ready";   state_col = COL_DIM; }
        gfx_surf_draw_string_smooth(&gs, 260, y, sname, state_col, 1);

        /* CPU% */
        int cpu_x10 = t->sample_total
            ? (int)(t->prev_ticks * 1000 / t->sample_total)
            : 0;
        snprintf(buf, sizeof(buf), "%d.%d", cpu_x10 / 10, cpu_x10 % 10);
        gfx_surf_draw_string_smooth(&gs, 340, y, buf,
                                     cpu_x10 > 0 ? COL_TEXT : COL_DIM, 1);

        /* Memory */
        if (t->mem_kb > 0) {
            snprintf(buf, sizeof(buf), "%dK", t->mem_kb);
            gfx_surf_draw_string_smooth(&gs, 410, y, buf, COL_TEXT, 1);
        } else {
            gfx_surf_draw_string_smooth(&gs, 410, y, "--", COL_DIM, 1);
        }

        /* Type */
        const char *type = t->is_pe ? "PE" : t->is_elf ? "ELF" :
                           t->is_user ? "User" : "Kern";
        gfx_surf_draw_string_smooth(&gs, 490, y, type, COL_DIM, 1);

        y += ROW_H;
    }

    ui_window_damage_all(tm_win_id);
}

/* ── Public API ─────────────────────────────────────────────────── */

void app_taskmgr_open(void) {
    if (tm_win_id >= 0) {
        ui_window_raise(tm_win_id);
        ui_window_focus(tm_win_id);
        return;
    }

    int sw = (int)gfx_width(), sh = (int)gfx_height();
    tm_win_id = ui_window_create((sw - WIN_W) / 2, (sh - WIN_H) / 2,
                                  WIN_W, WIN_H, "Task Manager");
    tm_last_refresh = 0;
    tm_paint();
}

int taskmgr_tick(int mx, int my, int btn_down, int btn_up) {
    if (tm_win_id < 0) return 0;

    if (ui_window_close_requested(tm_win_id)) {
        ui_window_close_clear(tm_win_id);
        ui_window_close_animated(tm_win_id);
        tm_win_id = -1;
        return 0;
    }

    /* Auto-refresh every ~1 second (120 ticks at 120Hz PIT) */
    uint32_t now = pit_get_ticks();
    if (now - tm_last_refresh >= 120) {
        tm_last_refresh = now;
        tm_paint();
    }

    /* Consume mouse clicks on content area */
    ui_win_info_t info = ui_window_info(tm_win_id);
    if (info.w <= 0) return 0;
    int lx = mx - info.cx, ly = my - info.cy;
    if (lx >= 0 && ly >= 0 && lx < info.cw && ly < info.ch) {
        if (btn_down) {
            ui_window_focus(tm_win_id);
            ui_window_raise(tm_win_id);
            return 1;
        }
        if (btn_up) return 1;
    }
    return 0;
}

int taskmgr_win_open(void) { return tm_win_id >= 0; }

/* Legacy stubs */
void app_taskmgr(void) { app_taskmgr_open(); }
ui_window_t *app_taskmgr_create(void) { return 0; }
void app_taskmgr_on_event(ui_window_t *w, ui_event_t *e) { (void)w; (void)e; }
void app_taskmgr_on_tick(ui_window_t *w) { (void)w; }
