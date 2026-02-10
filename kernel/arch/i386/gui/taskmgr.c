#include <kernel/taskmgr.h>
#include <kernel/gfx.h>
#include <kernel/wm.h>
#include <kernel/desktop.h>
#include <kernel/ui_widget.h>
#include <kernel/ui_theme.h>
#include <kernel/ui_event.h>
#include <kernel/mouse.h>
#include <kernel/idt.h>
#include <kernel/task.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ═══ Layout constants ════════════════════════════════════════ */

#define TM_HEADER_H    60    /* Summary bar height */
#define TM_COL_NAME    10    /* X offset for Name column */
#define TM_COL_CPU    280    /* X offset for CPU% column */
#define TM_COL_MEM    370    /* X offset for Memory column */
#define TM_COL_PID    450    /* X offset for PID column */
#define TM_COL_KILL   510    /* X offset for Kill column */
#define TM_ROW_H       22   /* Row height */
#define TM_TABLE_HDR_H 24   /* Table header row height */

/* Widget indices */
static int w_task_count_label;
static int w_mem_bar;
static int w_uptime_label;
static int w_task_table;

/* Task snapshot */
#define TM_MAX_ROWS 32
typedef struct {
    char name[32];
    int pid;
    int cpu_pct;     /* 0-100 */
    int mem_kb;
    int killable;
    int active;
} tm_row_t;

static tm_row_t rows[TM_MAX_ROWS];
static int row_count;
static int selected_row;
static int sort_col;     /* 0=name, 1=cpu, 2=mem, 3=pid */

/* Dynamic strings */
static char task_count_str[48];
static char uptime_str[48];

/* ═══ Snapshot ════════════════════════════════════════════════ */

static void tm_snapshot(void) {
    row_count = 0;
    for (int i = 0; i < TASK_MAX && row_count < TM_MAX_ROWS; i++) {
        task_info_t *t = task_get(i);
        if (!t || !t->active) continue;
        tm_row_t *r = &rows[row_count];
        r->active = 1;
        strncpy(r->name, t->name, 31);
        r->name[31] = '\0';
        r->pid = t->pid;
        r->mem_kb = t->mem_kb;
        r->killable = t->killable;
        /* CPU%: prev_ticks / sample_total * 100 */
        if (t->sample_total > 0)
            r->cpu_pct = (int)((uint64_t)t->prev_ticks * 100 / t->sample_total);
        else
            r->cpu_pct = 0;
        row_count++;
    }

    /* Sort */
    for (int i = 0; i < row_count - 1; i++) {
        for (int j = i + 1; j < row_count; j++) {
            int swap = 0;
            switch (sort_col) {
            case 0: swap = strcmp(rows[i].name, rows[j].name) > 0; break;
            case 1: swap = rows[i].cpu_pct < rows[j].cpu_pct; break;
            case 2: swap = rows[i].mem_kb < rows[j].mem_kb; break;
            case 3: swap = rows[i].pid > rows[j].pid; break;
            }
            if (swap) {
                tm_row_t tmp = rows[i];
                rows[i] = rows[j];
                rows[j] = tmp;
            }
        }
    }

    if (selected_row >= row_count) selected_row = row_count - 1;
    if (selected_row < 0) selected_row = 0;
}

/* ═══ Custom draw: task table ═════════════════════════════════ */

static void tm_draw_table(ui_window_t *win, int widget_idx,
                          uint32_t *canvas, int cw, int ch) {
    ui_widget_t *wg = ui_get_widget(win, widget_idx);
    if (!wg) return;
    int x0 = wg->x, y0 = wg->y;
    int w = wg->w, h = wg->h;

    /* Clear table area */
    gfx_buf_fill_rect(canvas, cw, ch, x0, y0, w, h, ui_theme.win_bg);

    /* Column headers */
    uint32_t hdr_bg = ui_theme.surface;
    gfx_buf_fill_rect(canvas, cw, ch, x0, y0, w, TM_TABLE_HDR_H, hdr_bg);
    gfx_buf_draw_string(canvas, cw, ch, x0 + TM_COL_NAME, y0 + 4,
                         "NAME", ui_theme.text_secondary, hdr_bg);
    gfx_buf_draw_string(canvas, cw, ch, x0 + TM_COL_CPU, y0 + 4,
                         "CPU%", ui_theme.text_secondary, hdr_bg);
    gfx_buf_draw_string(canvas, cw, ch, x0 + TM_COL_MEM, y0 + 4,
                         "MEM", ui_theme.text_secondary, hdr_bg);
    gfx_buf_draw_string(canvas, cw, ch, x0 + TM_COL_PID, y0 + 4,
                         "PID", ui_theme.text_secondary, hdr_bg);
    /* Separator under header */
    gfx_buf_fill_rect(canvas, cw, ch, x0, y0 + TM_TABLE_HDR_H - 1, w, 1,
                       ui_theme.border);

    /* Sort indicator */
    const char *sort_labels[] = { "NAME", "CPU%", "MEM", "PID" };
    int sort_x[] = { x0 + TM_COL_NAME, x0 + TM_COL_CPU, x0 + TM_COL_MEM, x0 + TM_COL_PID };
    int label_w = (int)strlen(sort_labels[sort_col]) * FONT_W;
    gfx_buf_fill_rect(canvas, cw, ch, sort_x[sort_col],
                       y0 + TM_TABLE_HDR_H - 2, label_w, 2, ui_theme.accent);

    /* Rows */
    int table_y = y0 + TM_TABLE_HDR_H;
    int visible_rows = (h - TM_TABLE_HDR_H) / TM_ROW_H;

    for (int i = 0; i < row_count && i < visible_rows; i++) {
        tm_row_t *r = &rows[i];
        int ry = table_y + i * TM_ROW_H;
        int selected = (i == selected_row);

        /* Row background */
        uint32_t row_bg = selected ? ui_theme.list_sel_bg :
                          ((i % 2) ? GFX_RGB(18, 18, 30) : ui_theme.win_bg);
        gfx_buf_fill_rect(canvas, cw, ch, x0, ry, w, TM_ROW_H, row_bg);

        /* Selection accent */
        if (selected)
            gfx_buf_fill_rect(canvas, cw, ch, x0 + 2, ry + 3, 3, TM_ROW_H - 6,
                               ui_theme.accent);

        /* Name */
        gfx_buf_draw_string(canvas, cw, ch, x0 + TM_COL_NAME,
                             ry + (TM_ROW_H - FONT_H) / 2,
                             r->name, ui_theme.text_primary, row_bg);

        /* CPU% with colored bar */
        int bar_w = 60;
        int bar_h = 10;
        int bar_x = x0 + TM_COL_CPU;
        int bar_y = ry + (TM_ROW_H - bar_h) / 2;
        gfx_buf_fill_rect(canvas, cw, ch, bar_x, bar_y, bar_w, bar_h,
                           ui_theme.progress_bg);
        if (r->cpu_pct > 0) {
            int fill = bar_w * r->cpu_pct / 100;
            if (fill > bar_w) fill = bar_w;
            uint32_t fill_c = (r->cpu_pct > 80) ? ui_theme.danger :
                              (r->cpu_pct > 50) ? ui_theme.progress_warn :
                              ui_theme.accent;
            gfx_buf_fill_rect(canvas, cw, ch, bar_x, bar_y, fill, bar_h, fill_c);
        }
        char cpu_str[8];
        snprintf(cpu_str, sizeof(cpu_str), "%d%%", r->cpu_pct);
        gfx_buf_draw_string(canvas, cw, ch, bar_x + bar_w + 4,
                             ry + (TM_ROW_H - FONT_H) / 2,
                             cpu_str, ui_theme.text_sub, row_bg);

        /* Memory */
        char mem_str2[16];
        if (r->mem_kb >= 1024)
            snprintf(mem_str2, sizeof(mem_str2), "%dMB", r->mem_kb / 1024);
        else
            snprintf(mem_str2, sizeof(mem_str2), "%dKB", r->mem_kb);
        gfx_buf_draw_string(canvas, cw, ch, x0 + TM_COL_MEM,
                             ry + (TM_ROW_H - FONT_H) / 2,
                             mem_str2, ui_theme.text_sub, row_bg);

        /* PID */
        char pid_str[8];
        snprintf(pid_str, sizeof(pid_str), "%d", r->pid);
        gfx_buf_draw_string(canvas, cw, ch, x0 + TM_COL_PID,
                             ry + (TM_ROW_H - FONT_H) / 2,
                             pid_str, ui_theme.text_sub, row_bg);

        /* Kill indicator for killable selected row */
        if (selected && r->killable) {
            gfx_buf_fill_rect(canvas, cw, ch, x0 + TM_COL_KILL,
                               ry + 3, 40, TM_ROW_H - 6, ui_theme.danger);
            gfx_buf_draw_string(canvas, cw, ch, x0 + TM_COL_KILL + 4,
                                 ry + (TM_ROW_H - FONT_H) / 2,
                                 "Kill", GFX_RGB(255, 255, 255), ui_theme.danger);
        }
    }
}

static int tm_table_event(ui_window_t *win, int widget_idx, ui_event_t *ev) {
    (void)win;
    (void)widget_idx;

    if (ev->type == UI_EVENT_MOUSE_DOWN) {
        ui_widget_t *wg = ui_get_widget(win, widget_idx);
        if (!wg) return 0;
        int wy = ev->mouse.wy - wg->y - TM_TABLE_HDR_H;
        if (wy >= 0) {
            int clicked = wy / TM_ROW_H;
            if (clicked >= 0 && clicked < row_count) {
                /* Double-click on kill area */
                if (clicked == selected_row && rows[clicked].killable) {
                    int wx = ev->mouse.wx - wg->x;
                    if (wx >= TM_COL_KILL && wx < TM_COL_KILL + 40) {
                        task_kill_by_pid(rows[clicked].pid);
                        tm_snapshot();
                        return 1;
                    }
                }
                selected_row = clicked;
                return 1;
            }
        }
    }
    return 0;
}

/* ═══ Refresh ═════════════════════════════════════════════════ */

static void tm_refresh(ui_window_t *win) {
    tm_snapshot();

    /* Task count */
    snprintf(task_count_str, sizeof(task_count_str), "Tasks: %d", row_count);
    ui_widget_t *wg = ui_get_widget(win, w_task_count_label);
    if (wg) strncpy(wg->label.text, task_count_str, UI_TEXT_MAX - 1);

    /* Memory */
    size_t used = heap_used();
    size_t total = heap_total();
    int pct = total > 0 ? (int)((uint64_t)used * 100 / total) : 0;
    wg = ui_get_widget(win, w_mem_bar);
    if (wg) wg->progress.value = pct;

    /* Uptime */
    uint32_t ticks = pit_get_ticks();
    uint32_t secs = ticks / 100;
    snprintf(uptime_str, sizeof(uptime_str), "Up %dh%dm%ds",
             (int)(secs / 3600), (int)((secs % 3600) / 60), (int)(secs % 60));
    wg = ui_get_widget(win, w_uptime_label);
    if (wg) strncpy(wg->label.text, uptime_str, UI_TEXT_MAX - 1);

    win->dirty = 1;
}

/* ═══ Event handler ═══════════════════════════════════════════ */

void app_taskmgr_on_event(ui_window_t *win, ui_event_t *ev) {
    if (ev->type == UI_EVENT_KEY_PRESS) {
        char key = ev->key.key;

        /* Sort hotkeys */
        if (key == 'n') { sort_col = 0; tm_refresh(win); return; }
        if (key == 'c') { sort_col = 1; tm_refresh(win); return; }
        if (key == 'm') { sort_col = 2; tm_refresh(win); return; }
        if (key == 'p') { sort_col = 3; tm_refresh(win); return; }

        /* Kill selected */
        if (key == 'k' && selected_row >= 0 && selected_row < row_count &&
            rows[selected_row].killable) {
            task_kill_by_pid(rows[selected_row].pid);
            tm_refresh(win);
            return;
        }

        /* Navigate */
        if (key == KEY_UP && selected_row > 0) {
            selected_row--;
            win->dirty = 1;
            return;
        }
        if (key == KEY_DOWN && selected_row < row_count - 1) {
            selected_row++;
            win->dirty = 1;
            return;
        }
    }

    /* Auto-refresh on every event (idle triggers ~30fps) */
    tm_refresh(win);
}

/* ═══ Create ══════════════════════════════════════════════════ */

ui_window_t *app_taskmgr_create(void) {
    int fb_w = (int)gfx_width(), fb_h = (int)gfx_height();
    int win_w = 700, win_h = 500;

    ui_window_t *win = ui_window_create(fb_w / 2 - win_w / 2,
                                         fb_h / 2 - win_h / 2 - 20,
                                         win_w, win_h, "Task Manager");
    if (!win) return 0;

    int cw, ch;
    wm_get_canvas(win->wm_id, &cw, &ch);
    int pad = ui_theme.padding;

    sort_col = 1;  /* Default sort by CPU */
    selected_row = 0;

    /* Summary bar */
    ui_add_card(win, 0, 0, cw, TM_HEADER_H, NULL, ui_theme.surface, 0);
    w_task_count_label = ui_add_label(win, pad, 8, 120, 20, "Tasks: 0", 0);
    w_mem_bar = ui_add_progress(win, pad + 130, 10, 200, 12, 0, NULL);
    w_uptime_label = ui_add_label(win, cw - 200, 8, 190, 20, "", ui_theme.text_sub);

    /* Sort hint */
    ui_add_label(win, pad, TM_HEADER_H - 20, cw - 2 * pad, 16,
                 "Sort: n=name c=cpu m=mem p=pid | k=kill | Up/Down=select",
                 ui_theme.text_dim);

    /* Separator */
    ui_add_separator(win, 0, TM_HEADER_H - 1, cw);

    /* Custom task table */
    w_task_table = ui_add_custom(win, 0, TM_HEADER_H, cw, ch - TM_HEADER_H,
                                  tm_draw_table, tm_table_event, NULL);

    tm_refresh(win);

    /* Auto-focus custom table */
    if (win->focused_widget < 0)
        ui_focus_next(win);

    return win;
}

/* ═══ Standalone entry point ══════════════════════════════════ */

void app_taskmgr(void) {
    ui_window_t *win = app_taskmgr_create();
    if (!win) return;
    ui_app_run(win, app_taskmgr_on_event);
    ui_window_destroy(win);
}
