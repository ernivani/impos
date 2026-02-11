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
#include <kernel/fs.h>
#include <kernel/net.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ═══ Layout constants ════════════════════════════════════════ */

#define TM_HEADER_H    158   /* Summary bar height (expanded for stats) */
#define TM_COL_NAME    10    /* X offset for Name column */
#define TM_COL_STATE  180    /* X offset for State column */
#define TM_COL_CPU    200    /* X offset for CPU% column */
#define TM_COL_GPU    310    /* X offset for GPU% column */
#define TM_COL_MEM    400    /* X offset for Memory column */
#define TM_COL_TIME   470    /* X offset for TIME+ column */
#define TM_COL_PID    560    /* X offset for PID column */
#define TM_COL_KILL   600    /* X offset for Kill column */
#define TM_ROW_H       22   /* Row height */
#define TM_TABLE_HDR_H 24   /* Table header row height */

/* Widget indices */
static int w_task_count_label;
static int w_cpu_bar;
static int w_cpu_label;
static int w_uptime_label;
static int w_mem_bar;
static int w_mem_label;
static int w_gpu_bar;
static int w_gpu_label;
static int w_disk_label;
static int w_net_label;
static int w_task_table;

/* Task snapshot */
#define TM_MAX_ROWS 32
typedef struct {
    char name[32];
    int pid;
    int cpu_pct;        /* 0-100 */
    int cpu_pct_x10;    /* 0-1000 for decimal display */
    int gpu_pct;        /* 0-100 */
    int mem_kb;
    int killable;
    int active;
    char state;         /* R=running, S=sleeping, I=idle */
    uint32_t total_ticks;  /* cumulative for TIME+ */
} tm_row_t;

static tm_row_t rows[TM_MAX_ROWS];
static int row_count;
static int selected_row;
static int selected_pid = -1;  /* track selection by PID across sort changes */
static int sort_col;     /* 0=name, 1=cpu, 2=mem, 3=pid */

/* Dynamic strings */
static char task_count_str[64];
static char uptime_str[48];
static char cpu_label_str[64];
static char mem_label_str[64];
static char gpu_label_str[64];
static char disk_label_str[80];
static char net_label_str[80];

/* ═══ Snapshot ════════════════════════════════════════════════ */

static void tm_snapshot(void) {
    row_count = 0;
    for (int i = 0; i < TASK_MAX && row_count < TM_MAX_ROWS; i++) {
        if (i == TASK_IDLE) continue;
        task_info_t *t = task_get(i);
        if (!t || !t->active) continue;
        tm_row_t *r = &rows[row_count];
        r->active = 1;
        strncpy(r->name, t->name, 31);
        r->name[31] = '\0';
        r->pid = t->pid;
        r->mem_kb = t->mem_kb;
        r->killable = t->killable;
        r->total_ticks = t->total_ticks;
        /* CPU%: prev_ticks / sample_total * 100 */
        if (t->sample_total > 0) {
            r->cpu_pct = (int)((uint64_t)t->prev_ticks * 100 / t->sample_total);
            r->cpu_pct_x10 = (int)((uint64_t)t->prev_ticks * 1000 / t->sample_total);
        } else {
            r->cpu_pct = 0;
            r->cpu_pct_x10 = 0;
        }
        /* GPU%: gpu_prev_ticks / gpu_sample_total * 100 */
        if (t->gpu_sample_total > 0) {
            r->gpu_pct = (int)((uint64_t)t->gpu_prev_ticks * 100 / t->gpu_sample_total);
        } else {
            r->gpu_pct = 0;
        }
        /* State */
        r->state = (r->cpu_pct_x10 > 0) ? 'R' : 'S';
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
            case 4: swap = rows[i].gpu_pct < rows[j].gpu_pct; break;
            }
            if (swap) {
                tm_row_t tmp = rows[i];
                rows[i] = rows[j];
                rows[j] = tmp;
            }
        }
    }

    /* Restore selection by PID */
    if (selected_pid >= 0) {
        selected_row = 0;
        for (int i = 0; i < row_count; i++) {
            if (rows[i].pid == selected_pid) { selected_row = i; break; }
        }
    }
    if (selected_row >= row_count) selected_row = row_count - 1;
    if (selected_row < 0) selected_row = 0;
    if (selected_row < row_count)
        selected_pid = rows[selected_row].pid;
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
    gfx_buf_draw_string(canvas, cw, ch, x0 + TM_COL_STATE, y0 + 4,
                         "S", ui_theme.text_secondary, hdr_bg);
    gfx_buf_draw_string(canvas, cw, ch, x0 + TM_COL_CPU, y0 + 4,
                         "CPU%", ui_theme.text_secondary, hdr_bg);
    gfx_buf_draw_string(canvas, cw, ch, x0 + TM_COL_GPU, y0 + 4,
                         "GPU%", ui_theme.text_secondary, hdr_bg);
    gfx_buf_draw_string(canvas, cw, ch, x0 + TM_COL_MEM, y0 + 4,
                         "MEM", ui_theme.text_secondary, hdr_bg);
    gfx_buf_draw_string(canvas, cw, ch, x0 + TM_COL_TIME, y0 + 4,
                         "TIME+", ui_theme.text_secondary, hdr_bg);
    gfx_buf_draw_string(canvas, cw, ch, x0 + TM_COL_PID, y0 + 4,
                         "PID", ui_theme.text_secondary, hdr_bg);
    /* Separator under header */
    gfx_buf_fill_rect(canvas, cw, ch, x0, y0 + TM_TABLE_HDR_H - 1, w, 1,
                       ui_theme.border);

    /* Sort indicator */
    const char *sort_labels[] = { "NAME", "CPU%", "MEM", "PID", "GPU%" };
    int sort_x[] = { x0 + TM_COL_NAME, x0 + TM_COL_CPU, x0 + TM_COL_MEM, x0 + TM_COL_PID, x0 + TM_COL_GPU };
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

        /* State */
        {
            char st[2] = { r->state, '\0' };
            uint32_t sc = (r->state == 'R') ? ui_theme.success :
                          (r->state == 'I') ? GFX_RGB(100, 140, 255) :
                          ui_theme.text_dim;
            gfx_buf_draw_string(canvas, cw, ch, x0 + TM_COL_STATE,
                                 ry + (TM_ROW_H - FONT_H) / 2, st, sc, row_bg);
        }

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

        /* GPU% with colored bar */
        {
            int gbar_x = x0 + TM_COL_GPU;
            gfx_buf_fill_rect(canvas, cw, ch, gbar_x, bar_y, bar_w, bar_h,
                               ui_theme.progress_bg);
            if (r->gpu_pct > 0) {
                int gfill = bar_w * r->gpu_pct / 100;
                if (gfill > bar_w) gfill = bar_w;
                uint32_t gc = (r->gpu_pct > 80) ? ui_theme.danger :
                              (r->gpu_pct > 50) ? ui_theme.progress_warn :
                              GFX_RGB(80, 180, 255);
                gfx_buf_fill_rect(canvas, cw, ch, gbar_x, bar_y, gfill, bar_h, gc);
            }
            char gpu_str[8];
            snprintf(gpu_str, sizeof(gpu_str), "%d%%", r->gpu_pct);
            gfx_buf_draw_string(canvas, cw, ch, gbar_x + bar_w + 4,
                                 ry + (TM_ROW_H - FONT_H) / 2,
                                 gpu_str, ui_theme.text_sub, row_bg);
        }

        /* Memory */
        char mem_str2[16];
        if (r->mem_kb >= 1024)
            snprintf(mem_str2, sizeof(mem_str2), "%dMB", r->mem_kb / 1024);
        else
            snprintf(mem_str2, sizeof(mem_str2), "%dKB", r->mem_kb);
        gfx_buf_draw_string(canvas, cw, ch, x0 + TM_COL_MEM,
                             ry + (TM_ROW_H - FONT_H) / 2,
                             mem_str2, ui_theme.text_sub, row_bg);

        /* TIME+ */
        {
            uint32_t tsecs = r->total_ticks / 120;
            uint32_t tcs = (r->total_ticks % 120) * 100 / 120;
            char time_str[16];
            snprintf(time_str, sizeof(time_str), "%d:%02d.%02d",
                     (int)(tsecs / 60), (int)(tsecs % 60), (int)tcs);
            gfx_buf_draw_string(canvas, cw, ch, x0 + TM_COL_TIME,
                                 ry + (TM_ROW_H - FONT_H) / 2,
                                 time_str, ui_theme.text_sub, row_bg);
        }

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
                selected_pid = rows[clicked].pid;
                return 1;
            }
        }
    }
    return 0;
}

/* ═══ Refresh ═════════════════════════════════════════════════ */

static void tm_refresh(ui_window_t *win) {
    tm_snapshot();
    ui_widget_t *wg;

    /* Task count with state breakdown */
    int n_running = 0, n_sleeping = 0;
    for (int i = 0; i < row_count; i++) {
        if (rows[i].state == 'R') n_running++;
        else n_sleeping++;
    }
    snprintf(task_count_str, sizeof(task_count_str),
             "Tasks: %d (%d run, %d slp)", row_count, n_running, n_sleeping);
    wg = ui_get_widget(win, w_task_count_label);
    if (wg) strncpy(wg->label.text, task_count_str, UI_TEXT_MAX - 1);

    /* CPU bar + label */
    task_info_t *idle_t = task_get(TASK_IDLE);
    int idle_x10 = 0, user_x10 = 0, sys_x10 = 0;
    if (idle_t && idle_t->sample_total > 0)
        idle_x10 = (int)(idle_t->prev_ticks * 1000 / idle_t->sample_total);
    for (int i = 1; i < TASK_MAX; i++) {
        task_info_t *t = task_get(i);
        if (!t || !t->active) continue;
        int pct_x10 = t->sample_total > 0
            ? (int)(t->prev_ticks * 1000 / t->sample_total) : 0;
        if (t->killable) user_x10 += pct_x10;
        else sys_x10 += pct_x10;
    }
    int cpu_pct = (1000 - idle_x10) / 10;
    if (cpu_pct < 0) cpu_pct = 0;
    if (cpu_pct > 100) cpu_pct = 100;

    wg = ui_get_widget(win, w_cpu_bar);
    if (wg) wg->progress.value = cpu_pct;

    snprintf(cpu_label_str, sizeof(cpu_label_str),
             "%d%% (%d.%d us, %d.%d sy, %d.%d id)",
             cpu_pct, user_x10 / 10, user_x10 % 10,
             sys_x10 / 10, sys_x10 % 10,
             idle_x10 / 10, idle_x10 % 10);
    wg = ui_get_widget(win, w_cpu_label);
    if (wg) strncpy(wg->label.text, cpu_label_str, UI_TEXT_MAX - 1);

    /* Memory bar + label */
    size_t used = heap_used();
    size_t total = heap_total();
    int mem_pct = total > 0 ? (int)((uint64_t)used * 100 / total) : 0;
    wg = ui_get_widget(win, w_mem_bar);
    if (wg) wg->progress.value = mem_pct;

    uint32_t ram_mb = gfx_get_system_ram_mb();
    int used_x10 = (int)(used / (1024 * 1024 / 10));
    size_t free_mem = total > used ? total - used : 0;
    int free_x10 = (int)(free_mem / (1024 * 1024 / 10));
    snprintf(mem_label_str, sizeof(mem_label_str),
             "%d.%dMiB / %dMiB (%d.%dMiB free)",
             used_x10 / 10, used_x10 % 10, (int)ram_mb,
             free_x10 / 10, free_x10 % 10);
    wg = ui_get_widget(win, w_mem_label);
    if (wg) strncpy(wg->label.text, mem_label_str, UI_TEXT_MAX - 1);

    /* Uptime */
    uint32_t ticks = pit_get_ticks();
    uint32_t secs = ticks / 120;
    snprintf(uptime_str, sizeof(uptime_str), "Up %dh%dm%ds",
             (int)(secs / 3600), (int)((secs % 3600) / 60), (int)(secs % 60));
    wg = ui_get_widget(win, w_uptime_label);
    if (wg) strncpy(wg->label.text, uptime_str, UI_TEXT_MAX - 1);

    /* GPU bar + label */
    int gpu_pct = (int)wm_get_gpu_usage();
    wg = ui_get_widget(win, w_gpu_bar);
    if (wg) wg->progress.value = gpu_pct;

    snprintf(gpu_label_str, sizeof(gpu_label_str),
             "%d%%  FPS:%d  %dx%d  VRAM:%dKB",
             gpu_pct, (int)wm_get_fps(),
             (int)gfx_width(), (int)gfx_height(),
             (int)(gfx_width() * gfx_height() * (gfx_bpp() / 8) / 1024));
    wg = ui_get_widget(win, w_gpu_label);
    if (wg) strncpy(wg->label.text, gpu_label_str, UI_TEXT_MAX - 1);

    /* Disk stats */
    int used_inodes = 0, used_blocks = 0;
    for (int i = 0; i < NUM_INODES; i++) {
        inode_t tmp;
        if (fs_read_inode(i, &tmp) == 0 && tmp.type != INODE_FREE) {
            used_inodes++;
            used_blocks += tmp.num_blocks;
            if (tmp.indirect_block) used_blocks++;
        }
    }
    uint32_t rd_ops = 0, rd_bytes = 0, wr_ops = 0, wr_bytes = 0;
    fs_get_io_stats(&rd_ops, &rd_bytes, &wr_ops, &wr_bytes);
    snprintf(disk_label_str, sizeof(disk_label_str),
             "Disk: %d/%d inodes  %d/%d blk (%dKB)  R:%d W:%d",
             used_inodes, NUM_INODES, used_blocks, NUM_BLOCKS,
             (int)(used_blocks * BLOCK_SIZE / 1024),
             (int)rd_ops, (int)wr_ops);
    wg = ui_get_widget(win, w_disk_label);
    if (wg) strncpy(wg->label.text, disk_label_str, UI_TEXT_MAX - 1);

    /* Net stats */
    uint32_t tx_p = 0, tx_b = 0, rx_p = 0, rx_b = 0;
    net_get_stats(&tx_p, &tx_b, &rx_p, &rx_b);
    snprintf(net_label_str, sizeof(net_label_str),
             "Net: TX %d pkts (%dKB)  RX %d pkts (%dKB)",
             (int)tx_p, (int)(tx_b / 1024),
             (int)rx_p, (int)(rx_b / 1024));
    wg = ui_get_widget(win, w_net_label);
    if (wg) strncpy(wg->label.text, net_label_str, UI_TEXT_MAX - 1);

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
        if (key == 'g') { sort_col = 4; tm_refresh(win); return; }

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
            selected_pid = rows[selected_row].pid;
            win->dirty = 1;
            return;
        }
        if (key == KEY_DOWN && selected_row < row_count - 1) {
            selected_row++;
            selected_pid = rows[selected_row].pid;
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
    int win_w = 750, win_h = 550;

    ui_window_t *win = ui_window_create(fb_w / 2 - win_w / 2,
                                         fb_h / 2 - win_h / 2 - 20,
                                         win_w, win_h, "Activity Monitor");
    if (!win) return 0;

    int cw, ch;
    wm_get_canvas(win->wm_id, &cw, &ch);
    int pad = ui_theme.padding;

    sort_col = 1;  /* Default sort by CPU */
    selected_row = 0;

    /* Summary card background */
    ui_add_card(win, 0, 0, cw, TM_HEADER_H, NULL, ui_theme.surface, 0);

    /* Row 1 (y=6): Tasks count | Uptime (right-aligned) */
    w_task_count_label = ui_add_label(win, pad, 6, 300, 16, "Tasks: 0", 0);
    w_uptime_label = ui_add_label(win, cw - 160, 6, 150, 16, "", ui_theme.text_sub);

    /* Row 2 (y=26): CPU bar + breakdown */
    ui_add_label(win, pad, 26, 32, 12, "CPU", ui_theme.text_secondary);
    w_cpu_bar = ui_add_progress(win, pad + 36, 28, 160, 10, 0, NULL);
    w_cpu_label = ui_add_label(win, pad + 204, 26, cw - pad - 210, 16, "", ui_theme.text_sub);

    /* Row 3 (y=44): Memory bar + info */
    ui_add_label(win, pad, 44, 32, 12, "Mem", ui_theme.text_secondary);
    w_mem_bar = ui_add_progress(win, pad + 36, 46, 160, 10, 0, NULL);
    w_mem_label = ui_add_label(win, pad + 204, 44, cw - pad - 210, 16, "", ui_theme.text_sub);

    /* Row 4 (y=62): GPU bar + info */
    ui_add_label(win, pad, 62, 32, 12, "GPU", ui_theme.text_secondary);
    w_gpu_bar = ui_add_progress(win, pad + 36, 64, 160, 10, 0, NULL);
    w_gpu_label = ui_add_label(win, pad + 204, 62, cw - pad - 210, 16, "", ui_theme.text_sub);

    /* Row 5 (y=82): Disk stats */
    w_disk_label = ui_add_label(win, pad, 82, cw - 2 * pad, 16, "Disk: ...", ui_theme.text_sub);

    /* Row 6 (y=100): Net stats */
    w_net_label = ui_add_label(win, pad, 100, cw - 2 * pad, 16, "Net: ...", ui_theme.text_sub);

    /* Sort hint */
    ui_add_label(win, pad, TM_HEADER_H - 18, cw - 2 * pad, 16,
                 "Sort: n=name c=cpu g=gpu m=mem p=pid | k=kill | Up/Dn=sel",
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

/* ═══ Periodic tick (auto-refresh) ════════════════════════════ */

void app_taskmgr_on_tick(ui_window_t *win) {
    tm_refresh(win);
}

/* ═══ Standalone entry point ══════════════════════════════════ */

void app_taskmgr(void) {
    ui_window_t *win = app_taskmgr_create();
    if (!win) return;
    ui_app_run(win, app_taskmgr_on_event);
    ui_window_destroy(win);
}
