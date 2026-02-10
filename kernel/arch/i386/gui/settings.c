#include <kernel/settings_app.h>
#include <kernel/gfx.h>
#include <kernel/wm.h>
#include <kernel/desktop.h>
#include <kernel/ui_widget.h>
#include <kernel/ui_theme.h>
#include <kernel/ui_event.h>
#include <kernel/mouse.h>
#include <kernel/hostname.h>
#include <kernel/config.h>
#include <kernel/idt.h>
#include <string.h>
#include <stdio.h>

#define SET_ROWS 4

static int w_list;

static const char *row_items[SET_ROWS];
static char row_bufs[SET_ROWS][128];

static void build_items(void) {
    /* Keyboard */
    int layout = keyboard_get_layout();
    const char *name = (layout == KB_LAYOUT_FR) ? "FR (AZERTY)" : "US (QWERTY)";
    snprintf(row_bufs[0], 128, "Keyboard Layout        < %s >", name);
    row_items[0] = row_bufs[0];

    /* Hostname */
    const char *h = hostname_get();
    snprintf(row_bufs[1], 128, "Hostname               %s", h ? h : "unknown");
    row_items[1] = row_bufs[1];

    /* Display */
    snprintf(row_bufs[2], 128, "Display                %dx%d @ %dbpp",
             (int)gfx_width(), (int)gfx_height(), (int)gfx_bpp());
    row_items[2] = row_bufs[2];

    /* Date/Time */
    datetime_t dt;
    config_get_datetime(&dt);
    snprintf(row_bufs[3], 128, "Date / Time            %d-%d-%d %d:%d:%d",
             dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
    row_items[3] = row_bufs[3];
}

void app_settings_on_event(ui_window_t *win, ui_event_t *ev) {
    if (ev->type != UI_EVENT_KEY_PRESS) return;
    char key = ev->key.key;

    ui_widget_t *list = ui_get_widget(win, w_list);
    if (!list) return;

    if (key == KEY_LEFT || key == KEY_RIGHT) {
        if (list->list.selected == 0) {
            int layout = keyboard_get_layout();
            keyboard_set_layout(layout == KB_LAYOUT_FR ? KB_LAYOUT_US : KB_LAYOUT_FR);
            build_items();
            win->dirty = 1;
        }
    }

    /* Refresh datetime on every event */
    build_items();
    win->dirty = 1;
}

ui_window_t *app_settings_create(void) {
    int fb_w = (int)gfx_width(), fb_h = (int)gfx_height();
    int win_w = 500, win_h = 280;

    ui_window_t *win = ui_window_create(fb_w / 2 - win_w / 2,
                                         fb_h / 2 - win_h / 2 - 30,
                                         win_w, win_h, "Settings");
    if (!win) return 0;

    int cw, ch;
    wm_get_canvas(win->wm_id, &cw, &ch);

    build_items();

    /* Header */
    ui_add_panel(win, 0, 0, cw, 30, "Settings");
    ui_add_separator(win, 0, 29, cw);

    /* Settings list */
    w_list = ui_add_list(win, 0, 30, cw, ch - 50, row_items, SET_ROWS);

    /* Footer hint */
    ui_add_label(win, 8, ch - 20, cw - 16, 16,
                 "Up/Down: select  Left/Right: change  Esc: close",
                 GFX_RGB(60, 60, 60));

    /* Auto-focus first focusable widget */
    if (win->focused_widget < 0)
        ui_focus_next(win);

    return win;
}

void app_settings(void) {
    ui_window_t *win = app_settings_create();
    if (!win) return;
    ui_app_run(win, app_settings_on_event);
    ui_window_destroy(win);
}
