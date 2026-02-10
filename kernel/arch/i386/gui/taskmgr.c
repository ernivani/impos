#include <kernel/taskmgr.h>
#include <kernel/gfx.h>
#include <kernel/wm.h>
#include <kernel/desktop.h>
#include <kernel/ui_widget.h>
#include <kernel/ui_theme.h>
#include <kernel/ui_event.h>
#include <kernel/mouse.h>
#include <kernel/idt.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Widget indices */
static int w_mem_panel, w_mem_bar, w_mem_label, w_ram_label;
static int w_up_panel, w_up_label;
static int w_win_panel, w_win_list;

/* Dynamic strings */
static char mem_str[64];
static char ram_str[64];
static char up_str[64];
static char win_title_str[32];

#define MAX_WIN_ITEMS 16
static const char *win_items[MAX_WIN_ITEMS];
static char win_item_bufs[MAX_WIN_ITEMS][64];
static int win_item_count;

static void refresh_data(ui_window_t *win) {
    /* Memory */
    size_t used = heap_used();
    size_t total = heap_total();
    int pct = total > 0 ? (int)((uint64_t)used * 100 / total) : 0;
    snprintf(mem_str, sizeof(mem_str), "Heap: %dKB / %dKB",
             (int)(used / 1024), (int)(total / 1024));
    snprintf(ram_str, sizeof(ram_str), "Physical RAM: %dMB",
             (int)gfx_get_system_ram_mb());

    ui_widget_t *bar = ui_get_widget(win, w_mem_bar);
    if (bar) bar->progress.value = pct;
    ui_widget_t *ml = ui_get_widget(win, w_mem_label);
    if (ml) strncpy(ml->label.text, mem_str, UI_TEXT_MAX - 1);
    ui_widget_t *rl = ui_get_widget(win, w_ram_label);
    if (rl) strncpy(rl->label.text, ram_str, UI_TEXT_MAX - 1);

    /* Uptime */
    uint32_t ticks = pit_get_ticks();
    uint32_t secs = ticks / 100;
    snprintf(up_str, sizeof(up_str), "%dh %dm %ds",
             (int)(secs / 3600), (int)((secs % 3600) / 60), (int)(secs % 60));
    ui_widget_t *ul = ui_get_widget(win, w_up_label);
    if (ul) strncpy(ul->label.text, up_str, UI_TEXT_MAX - 1);

    /* Windows */
    int wcount = wm_get_window_count();
    snprintf(win_title_str, sizeof(win_title_str), "Windows (%d)", wcount);
    ui_widget_t *wp = ui_get_widget(win, w_win_panel);
    if (wp) strncpy(wp->panel.title, win_title_str, 47);

    win_item_count = 0;
    for (int i = 0; i < wcount && i < MAX_WIN_ITEMS; i++) {
        wm_window_t *w = wm_get_window_by_index(i);
        if (!w) continue;
        snprintf(win_item_bufs[win_item_count], 64, "%s%s  (%dx%d)",
                 (w->flags & WM_WIN_FOCUSED) ? "> " : "  ", w->title, w->w, w->h);
        win_items[win_item_count] = win_item_bufs[win_item_count];
        win_item_count++;
    }
    ui_widget_t *wl = ui_get_widget(win, w_win_list);
    if (wl) {
        wl->list.items = win_items;
        wl->list.count = win_item_count;
    }

    win->dirty = 1;
}

void app_taskmgr_on_event(ui_window_t *win, ui_event_t *ev) {
    (void)ev;
    refresh_data(win);
}

ui_window_t *app_taskmgr_create(void) {
    int fb_w = (int)gfx_width(), fb_h = (int)gfx_height();
    int win_w = 400;
    int win_h = fb_h - TASKBAR_H - 80;

    ui_window_t *win = ui_window_create(fb_w / 2 - win_w / 2, 30,
                                         win_w, win_h, "Task Manager");
    if (!win) return 0;

    int cw, ch;
    wm_get_canvas(win->wm_id, &cw, &ch);
    int y = 8;

    /* Memory section */
    w_mem_panel = ui_add_panel(win, 0, y, cw, 28, "Memory");
    y += 28;
    w_mem_bar = ui_add_progress(win, 8, y + 4, cw - 16, 14, 0, NULL);
    y += 22;
    w_mem_label = ui_add_label(win, 8, y, cw - 16, 20, "", ui_theme.text_sub);
    y += 20;
    w_ram_label = ui_add_label(win, 8, y, cw - 16, 20, "", ui_theme.text_sub);
    y += 28;

    /* Uptime section */
    w_up_panel = ui_add_panel(win, 0, y, cw, 28, "Uptime");
    y += 28;
    w_up_label = ui_add_label(win, 8, y + 4, cw - 16, 20, "", 0);
    y += 28;

    /* Windows section */
    w_win_panel = ui_add_panel(win, 0, y, cw, 28, "Windows");
    y += 28;
    w_win_list = ui_add_list(win, 0, y, cw, ch - y, NULL, 0);

    refresh_data(win);

    /* Auto-focus first focusable widget */
    if (win->focused_widget < 0)
        ui_focus_next(win);

    return win;
}

void app_taskmgr(void) {
    ui_window_t *win = app_taskmgr_create();
    if (!win) return;
    ui_app_run(win, app_taskmgr_on_event);
    ui_window_destroy(win);
}
