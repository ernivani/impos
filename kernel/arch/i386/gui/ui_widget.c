#include <kernel/ui_widget.h>
#include <kernel/ui_theme.h>
#include <kernel/ui_event.h>
#include <kernel/gfx.h>
#include <kernel/wm.h>
#include <kernel/desktop.h>
#include <kernel/mouse.h>
#include <kernel/idt.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ═══ Window lifecycle ════════════════════════════════════════════ */

static ui_window_t windows[8];
static int ui_win_count;

ui_window_t *ui_window_create(int x, int y, int w, int h, const char *title) {
    if (ui_win_count >= 8) return 0;
    ui_window_t *win = &windows[ui_win_count++];
    memset(win, 0, sizeof(*win));
    win->wm_id = wm_create_window(x, y, w, h, title);
    if (win->wm_id < 0) { ui_win_count--; return 0; }
    win->focused_widget = -1;
    win->dirty = 1;
    wm_get_canvas(win->wm_id, &win->prev_cw, &win->prev_ch);
    return win;
}

void ui_window_destroy(ui_window_t *win) {
    if (!win) return;
    wm_destroy_window(win->wm_id);
    /* Remove from array */
    int idx = (int)(win - windows);
    if (idx >= 0 && idx < ui_win_count) {
        for (int i = idx; i < ui_win_count - 1; i++)
            windows[i] = windows[i + 1];
        ui_win_count--;
    }
}

/* ═══ Widget creation helpers ════════════════════════════════════ */

static int alloc_widget(ui_window_t *win) {
    if (!win || win->widget_count >= UI_MAX_WIDGETS) return -1;
    int idx = win->widget_count++;
    memset(&win->widgets[idx], 0, sizeof(ui_widget_t));
    win->widgets[idx].flags = UI_FLAG_VISIBLE;
    win->widgets[idx].parent = -1;
    return idx;
}

int ui_add_label(ui_window_t *win, int x, int y, int w, int h,
                 const char *text, uint32_t color) {
    int idx = alloc_widget(win);
    if (idx < 0) return -1;
    ui_widget_t *wg = &win->widgets[idx];
    wg->type = UI_LABEL;
    wg->x = x; wg->y = y; wg->w = w; wg->h = h;
    if (text) strncpy(wg->label.text, text, UI_TEXT_MAX - 1);
    wg->label.color = color;
    win->dirty = 1;
    return idx;
}

int ui_add_button(ui_window_t *win, int x, int y, int w, int h,
                  const char *text, ui_callback_t on_click) {
    int idx = alloc_widget(win);
    if (idx < 0) return -1;
    ui_widget_t *wg = &win->widgets[idx];
    wg->type = UI_BUTTON;
    wg->x = x; wg->y = y; wg->w = w; wg->h = h;
    wg->flags |= UI_FLAG_FOCUSABLE;
    if (text) strncpy(wg->button.text, text, 47);
    wg->button.on_click = on_click;
    win->dirty = 1;
    return idx;
}

int ui_add_textinput(ui_window_t *win, int x, int y, int w, int h,
                     const char *placeholder, int max_len, int is_password) {
    int idx = alloc_widget(win);
    if (idx < 0) return -1;
    ui_widget_t *wg = &win->widgets[idx];
    wg->type = UI_TEXTINPUT;
    wg->x = x; wg->y = y; wg->w = w; wg->h = h;
    wg->flags |= UI_FLAG_FOCUSABLE;
    if (placeholder) strncpy(wg->textinput.placeholder, placeholder, 47);
    wg->textinput.max_len = (max_len > 0 && max_len < UI_TEXT_MAX) ? max_len : UI_TEXT_MAX - 1;
    wg->textinput.password = is_password ? 1 : 0;
    win->dirty = 1;
    return idx;
}

int ui_add_list(ui_window_t *win, int x, int y, int w, int h,
                const char **items, int count) {
    int idx = alloc_widget(win);
    if (idx < 0) return -1;
    ui_widget_t *wg = &win->widgets[idx];
    wg->type = UI_LIST;
    wg->x = x; wg->y = y; wg->w = w; wg->h = h;
    wg->flags |= UI_FLAG_FOCUSABLE;
    wg->list.items = items;
    wg->list.count = count;
    wg->list.selected = 0;
    wg->list.scroll = 0;
    win->dirty = 1;
    return idx;
}

int ui_add_checkbox(ui_window_t *win, int x, int y, int w, int h,
                    const char *text, int checked) {
    int idx = alloc_widget(win);
    if (idx < 0) return -1;
    ui_widget_t *wg = &win->widgets[idx];
    wg->type = UI_CHECKBOX;
    wg->x = x; wg->y = y; wg->w = w; wg->h = h;
    wg->flags |= UI_FLAG_FOCUSABLE;
    if (text) strncpy(wg->checkbox.text, text, 47);
    wg->checkbox.checked = checked ? 1 : 0;
    win->dirty = 1;
    return idx;
}

int ui_add_progress(ui_window_t *win, int x, int y, int w, int h,
                    int value, const char *label) {
    int idx = alloc_widget(win);
    if (idx < 0) return -1;
    ui_widget_t *wg = &win->widgets[idx];
    wg->type = UI_PROGRESS;
    wg->x = x; wg->y = y; wg->w = w; wg->h = h;
    wg->progress.value = value;
    if (label) strncpy(wg->progress.label, label, 47);
    win->dirty = 1;
    return idx;
}

int ui_add_tabs(ui_window_t *win, int x, int y, int w, int h,
                const char **labels, int count) {
    int idx = alloc_widget(win);
    if (idx < 0) return -1;
    ui_widget_t *wg = &win->widgets[idx];
    wg->type = UI_TABS;
    wg->x = x; wg->y = y; wg->w = w; wg->h = h;
    wg->flags |= UI_FLAG_FOCUSABLE;
    wg->tabs.labels = labels;
    wg->tabs.count = count;
    wg->tabs.active = 0;
    win->dirty = 1;
    return idx;
}

int ui_add_panel(ui_window_t *win, int x, int y, int w, int h,
                 const char *title) {
    int idx = alloc_widget(win);
    if (idx < 0) return -1;
    ui_widget_t *wg = &win->widgets[idx];
    wg->type = UI_PANEL;
    wg->x = x; wg->y = y; wg->w = w; wg->h = h;
    if (title) strncpy(wg->panel.title, title, 47);
    win->dirty = 1;
    return idx;
}

int ui_add_separator(ui_window_t *win, int x, int y, int w) {
    int idx = alloc_widget(win);
    if (idx < 0) return -1;
    ui_widget_t *wg = &win->widgets[idx];
    wg->type = UI_SEPARATOR;
    wg->x = x; wg->y = y; wg->w = w; wg->h = 1;
    win->dirty = 1;
    return idx;
}

int ui_add_custom(ui_window_t *win, int x, int y, int w, int h,
                  ui_custom_draw_t draw_fn, ui_custom_event_t event_fn,
                  void *userdata) {
    int idx = alloc_widget(win);
    if (idx < 0) return -1;
    ui_widget_t *wg = &win->widgets[idx];
    wg->type = UI_CUSTOM;
    wg->x = x; wg->y = y; wg->w = w; wg->h = h;
    wg->custom.draw = draw_fn;
    wg->custom.event = event_fn;
    wg->custom.userdata = userdata;
    if (event_fn) wg->flags |= UI_FLAG_FOCUSABLE;
    win->dirty = 1;
    return idx;
}

int ui_add_toggle(ui_window_t *win, int x, int y, int w, int h,
                  const char *text, int on) {
    int idx = alloc_widget(win);
    if (idx < 0) return -1;
    ui_widget_t *wg = &win->widgets[idx];
    wg->type = UI_TOGGLE;
    wg->x = x; wg->y = y; wg->w = w; wg->h = h;
    wg->flags |= UI_FLAG_FOCUSABLE;
    if (text) strncpy(wg->toggle.text, text, 47);
    wg->toggle.on = on ? 1 : 0;
    win->dirty = 1;
    return idx;
}

int ui_add_icon_grid(ui_window_t *win, int x, int y, int w, int h,
                     int cols, int cell_w, int cell_h,
                     const char **labels, int count,
                     void (*draw_icon)(int idx, int x, int y, int sel)) {
    int idx = alloc_widget(win);
    if (idx < 0) return -1;
    ui_widget_t *wg = &win->widgets[idx];
    wg->type = UI_ICON_GRID;
    wg->x = x; wg->y = y; wg->w = w; wg->h = h;
    wg->flags |= UI_FLAG_FOCUSABLE;
    wg->icon_grid.cols = cols;
    wg->icon_grid.cell_w = cell_w;
    wg->icon_grid.cell_h = cell_h;
    wg->icon_grid.labels = labels;
    wg->icon_grid.count = count;
    wg->icon_grid.selected = 0;
    wg->icon_grid.scroll = 0;
    wg->icon_grid.draw_icon = draw_icon;
    win->dirty = 1;
    return idx;
}

int ui_add_card(ui_window_t *win, int x, int y, int w, int h,
                const char *title, uint32_t bg_color, int radius) {
    int idx = alloc_widget(win);
    if (idx < 0) return -1;
    ui_widget_t *wg = &win->widgets[idx];
    wg->type = UI_CARD;
    wg->x = x; wg->y = y; wg->w = w; wg->h = h;
    if (title) strncpy(wg->card.title, title, 47);
    wg->card.bg_color = bg_color ? bg_color : ui_theme.card_bg;
    wg->card.radius = radius > 0 ? radius : ui_theme.border_radius;
    win->dirty = 1;
    return idx;
}

ui_widget_t *ui_get_widget(ui_window_t *win, int idx) {
    if (!win || idx < 0 || idx >= win->widget_count) return 0;
    return &win->widgets[idx];
}

void ui_widget_set_visible(ui_window_t *win, int idx, int visible) {
    if (!win || idx < 0 || idx >= win->widget_count) return;
    if (visible)
        win->widgets[idx].flags |= UI_FLAG_VISIBLE;
    else
        win->widgets[idx].flags &= ~UI_FLAG_VISIBLE;
    win->dirty = 1;
}

void ui_widget_set_visible_range(ui_window_t *win, int from, int to, int visible) {
    if (!win) return;
    if (from < 0) from = 0;
    if (to > win->widget_count) to = win->widget_count;
    for (int i = from; i < to; i++) {
        if (visible)
            win->widgets[i].flags |= UI_FLAG_VISIBLE;
        else
            win->widgets[i].flags &= ~UI_FLAG_VISIBLE;
    }
    win->dirty = 1;
}

/* ═══ Drawing ════════════════════════════════════════════════════ */

static void draw_separator(ui_window_t *win, ui_widget_t *wg) {
    wm_fill_rect(win->wm_id, wg->x, wg->y, wg->w, 1, ui_theme.border);
}

static void draw_label(ui_window_t *win, ui_widget_t *wg) {
    uint32_t color = wg->label.color ? wg->label.color : ui_theme.text_primary;
    uint32_t bg = ui_theme.win_bg;
    wm_draw_string(win->wm_id, wg->x, wg->y + (wg->h - FONT_H) / 2,
                   wg->label.text, color, bg);
}

static void draw_panel(ui_window_t *win, ui_widget_t *wg) {
    wm_fill_rect(win->wm_id, wg->x, wg->y, wg->w, wg->h, ui_theme.surface);
    if (wg->panel.title[0]) {
        wm_draw_string(win->wm_id, wg->x + ui_theme.padding,
                       wg->y + (ui_theme.tab_height - FONT_H) / 2,
                       wg->panel.title, ui_theme.text_primary, ui_theme.surface);
    }
}

static void draw_progress(ui_window_t *win, ui_widget_t *wg) {
    int bar_h = wg->h > 14 ? 14 : wg->h;
    int bar_y = wg->y + (wg->h - bar_h) / 2;

    wm_fill_rect(win->wm_id, wg->x, bar_y, wg->w, bar_h, ui_theme.progress_bg);
    if (wg->progress.value > 0) {
        int fill = wg->w * wg->progress.value / 100;
        if (fill > wg->w) fill = wg->w;
        uint32_t fill_color = (wg->progress.value > 75) ?
            ui_theme.progress_warn : ui_theme.progress_fill;
        wm_fill_rect(win->wm_id, wg->x, bar_y, fill, bar_h, fill_color);
    }
    if (wg->progress.label[0]) {
        wm_draw_string(win->wm_id, wg->x, wg->y + wg->h + 2,
                       wg->progress.label, ui_theme.text_sub, ui_theme.win_bg);
    }
}

static void draw_button(ui_window_t *win, ui_widget_t *wg, int focused) {
    uint32_t bg, text_c;
    if (wg->button.primary) {
        bg = wg->button.pressed ? ui_theme.btn_primary_bg :
             (wg->flags & UI_FLAG_HOVER) ? ui_theme.btn_primary_hover :
             ui_theme.btn_primary_bg;
        text_c = ui_theme.btn_primary_text;
    } else {
        bg = wg->button.pressed ? ui_theme.btn_pressed :
             (wg->flags & UI_FLAG_HOVER) ? ui_theme.btn_hover :
             ui_theme.btn_bg;
        text_c = ui_theme.btn_text;
    }
    wm_fill_rect(win->wm_id, wg->x, wg->y, wg->w, wg->h, bg);
    if (focused)
        wm_draw_rect(win->wm_id, wg->x, wg->y, wg->w, wg->h, ui_theme.accent);
    else
        wm_draw_rect(win->wm_id, wg->x, wg->y, wg->w, wg->h, ui_theme.border);

    int tw = (int)strlen(wg->button.text) * FONT_W;
    int tx = wg->x + wg->w / 2 - tw / 2;
    int ty = wg->y + (wg->h - FONT_H) / 2;
    wm_draw_string(win->wm_id, tx, ty, wg->button.text, text_c, bg);
}

static void draw_checkbox(ui_window_t *win, ui_widget_t *wg, int focused) {
    int box_sz = 14;
    int bx = wg->x;
    int by = wg->y + (wg->h - box_sz) / 2;

    if (wg->checkbox.checked) {
        wm_fill_rect(win->wm_id, bx, by, box_sz, box_sz, ui_theme.checkbox_checked);
        /* Checkmark */
        wm_draw_line(win->wm_id, bx + 3, by + 7, bx + 6, by + 10, ui_theme.checkbox_check);
        wm_draw_line(win->wm_id, bx + 6, by + 10, bx + 11, by + 3, ui_theme.checkbox_check);
    } else {
        wm_fill_rect(win->wm_id, bx, by, box_sz, box_sz, ui_theme.checkbox_bg);
        wm_draw_rect(win->wm_id, bx, by, box_sz, box_sz, ui_theme.checkbox_border);
    }
    if (focused)
        wm_draw_rect(win->wm_id, bx - 1, by - 1, box_sz + 2, box_sz + 2, ui_theme.accent);

    wm_draw_string(win->wm_id, wg->x + box_sz + 8,
                   wg->y + (wg->h - FONT_H) / 2,
                   wg->checkbox.text, ui_theme.text_primary, ui_theme.win_bg);
}

static void draw_tabs(ui_window_t *win, ui_widget_t *wg, int focused) {
    int tab_w = wg->w / (wg->tabs.count > 0 ? wg->tabs.count : 1);
    for (int t = 0; t < wg->tabs.count; t++) {
        uint32_t tbg = (t == wg->tabs.active) ? ui_theme.tab_active_bg : ui_theme.tab_bg;
        uint32_t tc  = (t == wg->tabs.active) ? ui_theme.tab_active_text : ui_theme.tab_text;
        wm_fill_rect(win->wm_id, wg->x + t * tab_w, wg->y, tab_w, wg->h, tbg);
        int tw = (int)strlen(wg->tabs.labels[t]) * FONT_W;
        wm_draw_string(win->wm_id,
                       wg->x + t * tab_w + tab_w / 2 - tw / 2,
                       wg->y + (wg->h - FONT_H) / 2,
                       wg->tabs.labels[t], tc, tbg);
    }
    /* Bottom border */
    wm_fill_rect(win->wm_id, wg->x, wg->y + wg->h - 1, wg->w, 1, ui_theme.border);
    if (focused) {
        /* Active tab underline */
        int ax = wg->x + wg->tabs.active * tab_w;
        wm_fill_rect(win->wm_id, ax, wg->y + wg->h - 2, tab_w, 2, ui_theme.accent);
    }
}

static void draw_list(ui_window_t *win, ui_widget_t *wg, int focused) {
    int row_h = ui_theme.row_height;
    int visible = wg->h / row_h;
    int scroll = wg->list.scroll;

    wm_fill_rect(win->wm_id, wg->x, wg->y, wg->w, wg->h, ui_theme.win_bg);

    for (int i = 0; i < visible; i++) {
        int idx = scroll + i;
        int ry = wg->y + i * row_h;

        if (idx >= wg->list.count) {
            wm_fill_rect(win->wm_id, wg->x, ry, wg->w, row_h, ui_theme.win_bg);
            continue;
        }

        int selected = (idx == wg->list.selected);
        uint32_t bg = selected ? ui_theme.list_sel_bg :
                      ((idx % 2) ? GFX_RGB(12, 12, 12) : ui_theme.win_bg);
        wm_fill_rect(win->wm_id, wg->x, ry, wg->w, row_h, bg);

        if (selected && focused)
            wm_fill_rect(win->wm_id, wg->x + 2, ry + 4, 3, row_h - 8, ui_theme.accent);

        wm_draw_string(win->wm_id, wg->x + 10, ry + (row_h - FONT_H) / 2,
                       wg->list.items[idx],
                       selected ? ui_theme.text_primary : ui_theme.text_secondary, bg);
    }

    /* Scrollbar */
    if (wg->list.count > visible && visible > 0) {
        int sb_x = wg->x + wg->w - 4;
        int sb_h = wg->h * visible / wg->list.count;
        if (sb_h < 10) sb_h = 10;
        int sb_y = wg->y + (wg->h - sb_h) * scroll / (wg->list.count - visible);
        wm_fill_rect(win->wm_id, sb_x, sb_y, 3, sb_h, ui_theme.text_sub);
    }
}

static void draw_textinput(ui_window_t *win, ui_widget_t *wg, int focused) {
    uint32_t bg = ui_theme.input_bg;
    uint32_t border_c = focused ? ui_theme.input_border_focus : ui_theme.input_border;

    wm_fill_rect(win->wm_id, wg->x, wg->y, wg->w, wg->h, bg);
    wm_draw_rect(win->wm_id, wg->x, wg->y, wg->w, wg->h, border_c);

    int tx = wg->x + 8;
    int ty = wg->y + (wg->h - FONT_H) / 2;
    int len = (int)strlen(wg->textinput.text);
    int max_chars = (wg->w - 16) / FONT_W;

    if (len == 0 && !focused) {
        /* Placeholder */
        wm_draw_string(win->wm_id, tx, ty, wg->textinput.placeholder,
                       ui_theme.input_placeholder, bg);
    } else if (wg->textinput.password) {
        /* Draw dots for password */
        int vis = len - wg->textinput.scroll;
        if (vis > max_chars) vis = max_chars;
        for (int i = 0; i < vis; i++) {
            int dx = tx + i * 12 + 4;
            int dy = wg->y + wg->h / 2;
            /* Small filled circle */
            for (int cy = -3; cy <= 3; cy++)
                for (int cx = -3; cx <= 3; cx++)
                    if (cx*cx + cy*cy <= 9)
                        wm_put_pixel(win->wm_id, dx + cx, dy + cy, ui_theme.text_primary);
        }
    } else {
        /* Normal text */
        int start = wg->textinput.scroll;
        int vis = len - start;
        if (vis > max_chars) vis = max_chars;
        for (int i = 0; i < vis; i++) {
            wm_draw_char(win->wm_id, tx + i * FONT_W, ty,
                         wg->textinput.text[start + i],
                         ui_theme.text_primary, bg);
        }
    }

    /* Cursor */
    if (focused) {
        int cur_vis = wg->textinput.cursor - wg->textinput.scroll;
        if (cur_vis >= 0 && cur_vis <= max_chars) {
            int cx;
            if (wg->textinput.password)
                cx = tx + cur_vis * 12;
            else
                cx = tx + cur_vis * FONT_W;
            wm_fill_rect(win->wm_id, cx, ty, 1, FONT_H, ui_theme.dot);
        }
    }
}

static void draw_custom(ui_window_t *win, ui_widget_t *wg) {
    if (wg->custom.draw) {
        int cw, ch;
        uint32_t *canvas = wm_get_canvas(win->wm_id, &cw, &ch);
        wg->custom.draw(win, (int)(wg - win->widgets), canvas, cw, ch);
    }
}

static void draw_toggle(ui_window_t *win, ui_widget_t *wg, int focused) {
    /* Draw label text on the left */
    if (wg->toggle.text[0]) {
        wm_draw_string(win->wm_id, wg->x, wg->y + (wg->h - FONT_H) / 2,
                       wg->toggle.text, ui_theme.text_primary, ui_theme.win_bg);
    }

    /* Draw toggle pill on the right */
    int tw = 40, th = 22;
    int tx = wg->x + wg->w - tw - 4;
    int ty = wg->y + (wg->h - th) / 2;
    uint32_t pill_bg = wg->toggle.on ? ui_theme.toggle_on_bg : ui_theme.toggle_off_bg;

    wm_fill_rounded_rect(win->wm_id, tx, ty, tw, th, th / 2, pill_bg);

    /* Handle circle */
    int handle_r = (th - 6) / 2;
    int handle_cx = wg->toggle.on ? tx + tw - handle_r - 4 : tx + handle_r + 4;
    int handle_cy = ty + th / 2;
    /* Draw filled circle for handle */
    for (int dy = -handle_r; dy <= handle_r; dy++)
        for (int dx = -handle_r; dx <= handle_r; dx++)
            if (dx * dx + dy * dy <= handle_r * handle_r)
                wm_put_pixel(win->wm_id, handle_cx + dx, handle_cy + dy,
                             ui_theme.toggle_handle);

    if (focused)
        wm_draw_rect(win->wm_id, tx - 1, ty - 1, tw + 2, th + 2, ui_theme.accent);
}

static void draw_icon_grid(ui_window_t *win, ui_widget_t *wg, int focused) {
    (void)focused;
    int cols = wg->icon_grid.cols > 0 ? wg->icon_grid.cols : 4;
    int cw2 = wg->icon_grid.cell_w;
    int ch2 = wg->icon_grid.cell_h;

    /* Clear area */
    wm_fill_rect(win->wm_id, wg->x, wg->y, wg->w, wg->h, ui_theme.win_bg);

    for (int i = 0; i < wg->icon_grid.count; i++) {
        int col = i % cols;
        int row = i / cols;
        int cx = wg->x + col * cw2;
        int cy = wg->y + row * ch2 - wg->icon_grid.scroll;
        if (cy + ch2 < wg->y || cy > wg->y + wg->h) continue;

        int selected = (i == wg->icon_grid.selected);

        /* Selection highlight */
        if (selected) {
            wm_fill_rounded_rect_alpha(win->wm_id, cx + 2, cy + 2,
                                        cw2 - 4, ch2 - 4, 6,
                                        ui_theme.icon_grid_sel, 60);
        }

        /* Draw icon via callback */
        if (wg->icon_grid.draw_icon) {
            int icon_x = cx + (cw2 - 48) / 2;
            int icon_y = cy + 4;
            wg->icon_grid.draw_icon(i, icon_x, icon_y, selected);
        }

        /* Draw label below icon */
        if (wg->icon_grid.labels && wg->icon_grid.labels[i]) {
            const char *label = wg->icon_grid.labels[i];
            int lw = (int)strlen(label) * FONT_W;
            int lx = cx + (cw2 - lw) / 2;
            if (lx < cx + 2) lx = cx + 2;
            int ly = cy + ch2 - FONT_H - 4;
            wm_draw_string(win->wm_id, lx, ly, label,
                           selected ? ui_theme.text_primary : ui_theme.text_secondary,
                           ui_theme.win_bg);
        }
    }
}

static void draw_card(ui_window_t *win, ui_widget_t *wg) {
    uint32_t bg = wg->card.bg_color ? wg->card.bg_color : ui_theme.card_bg;
    int r = wg->card.radius > 0 ? wg->card.radius : ui_theme.border_radius;
    wm_fill_rounded_rect(win->wm_id, wg->x, wg->y, wg->w, wg->h, r, bg);

    /* Optional border */
    /* Draw outline manually: top/bottom/left/right rects (simple) */
    wm_draw_rect(win->wm_id, wg->x, wg->y, wg->w, wg->h, ui_theme.card_border);

    /* Title header */
    if (wg->card.title[0]) {
        wm_draw_string(win->wm_id, wg->x + ui_theme.padding,
                       wg->y + (24 - FONT_H) / 2 + 2,
                       wg->card.title, ui_theme.text_secondary, bg);
        /* Thin separator under title */
        wm_fill_rect(win->wm_id, wg->x + 4, wg->y + 24,
                     wg->w - 8, 1, ui_theme.card_border);
    }
}

void ui_window_redraw(ui_window_t *win) {
    if (!win) return;

    /* Clear canvas */
    wm_clear_canvas(win->wm_id, ui_theme.win_bg);

    /* Draw all visible widgets */
    for (int i = 0; i < win->widget_count; i++) {
        ui_widget_t *wg = &win->widgets[i];
        if (!(wg->flags & UI_FLAG_VISIBLE)) continue;

        int focused = (i == win->focused_widget);
        switch (wg->type) {
        case UI_SEPARATOR: draw_separator(win, wg); break;
        case UI_LABEL:     draw_label(win, wg); break;
        case UI_PANEL:     draw_panel(win, wg); break;
        case UI_PROGRESS:  draw_progress(win, wg); break;
        case UI_BUTTON:    draw_button(win, wg, focused); break;
        case UI_CHECKBOX:  draw_checkbox(win, wg, focused); break;
        case UI_TABS:      draw_tabs(win, wg, focused); break;
        case UI_LIST:      draw_list(win, wg, focused); break;
        case UI_TEXTINPUT: draw_textinput(win, wg, focused); break;
        case UI_CUSTOM:    draw_custom(win, wg); break;
        case UI_TOGGLE:    draw_toggle(win, wg, focused); break;
        case UI_ICON_GRID: draw_icon_grid(win, wg, focused); break;
        case UI_CARD:      draw_card(win, wg); break;
        }
    }

    win->dirty = 0;
}

/* ═══ Focus management ═══════════════════════════════════════════ */

void ui_focus_next(ui_window_t *win) {
    if (!win) return;
    int start = win->focused_widget + 1;
    for (int i = 0; i < win->widget_count; i++) {
        int idx = (start + i) % win->widget_count;
        if (win->widgets[idx].flags & UI_FLAG_FOCUSABLE) {
            /* Release capture from old widget */
            if (win->focused_widget >= 0)
                win->widgets[win->focused_widget].flags &= ~UI_FLAG_CAPTURING;
            win->focused_widget = idx;
            win->dirty = 1;
            return;
        }
    }
}

void ui_focus_prev(ui_window_t *win) {
    if (!win) return;
    int start = win->focused_widget - 1;
    if (start < 0) start = win->widget_count - 1;
    for (int i = 0; i < win->widget_count; i++) {
        int idx = (start - i + win->widget_count) % win->widget_count;
        if (win->widgets[idx].flags & UI_FLAG_FOCUSABLE) {
            if (win->focused_widget >= 0)
                win->widgets[win->focused_widget].flags &= ~UI_FLAG_CAPTURING;
            win->focused_widget = idx;
            win->dirty = 1;
            return;
        }
    }
}

/* ═══ Event dispatch ═════════════════════════════════════════════ */

static int point_in_widget(ui_widget_t *wg, int wx, int wy) {
    return (wx >= wg->x && wx < wg->x + wg->w &&
            wy >= wg->y && wy < wg->y + wg->h);
}

static void handle_textinput_key(ui_window_t *win, ui_widget_t *wg, int idx, char key) {
    int len = (int)strlen(wg->textinput.text);
    int max_chars = (wg->w - 16) / FONT_W;

    if (key == '\n') {
        wg->flags &= ~UI_FLAG_CAPTURING;
        if (wg->textinput.on_submit)
            wg->textinput.on_submit(win, idx);
        win->dirty = 1;
        return;
    }
    if (key == KEY_ESCAPE) {
        wg->flags &= ~UI_FLAG_CAPTURING;
        win->dirty = 1;
        return;
    }
    if (key == '\b') {
        if (wg->textinput.cursor > 0) {
            memmove(&wg->textinput.text[wg->textinput.cursor - 1],
                    &wg->textinput.text[wg->textinput.cursor],
                    len - wg->textinput.cursor + 1);
            wg->textinput.cursor--;
            if (wg->textinput.scroll > 0 && wg->textinput.cursor < wg->textinput.scroll)
                wg->textinput.scroll--;
        }
        win->dirty = 1;
        return;
    }
    if (key == KEY_LEFT) {
        if (wg->textinput.cursor > 0) {
            wg->textinput.cursor--;
            if (wg->textinput.cursor < wg->textinput.scroll)
                wg->textinput.scroll = wg->textinput.cursor;
        }
        win->dirty = 1;
        return;
    }
    if (key == KEY_RIGHT) {
        if (wg->textinput.cursor < len) {
            wg->textinput.cursor++;
            if (wg->textinput.cursor - wg->textinput.scroll > max_chars)
                wg->textinput.scroll++;
        }
        win->dirty = 1;
        return;
    }
    /* Filter special keys */
    if ((unsigned char)key >= 0xB0 && (unsigned char)key <= 0xBB) return;
    if (key < 32 || key >= 127) return;

    /* Insert character */
    if (len < wg->textinput.max_len) {
        memmove(&wg->textinput.text[wg->textinput.cursor + 1],
                &wg->textinput.text[wg->textinput.cursor],
                len - wg->textinput.cursor + 1);
        wg->textinput.text[wg->textinput.cursor] = key;
        wg->textinput.cursor++;
        if (wg->textinput.cursor - wg->textinput.scroll > max_chars)
            wg->textinput.scroll++;
        win->dirty = 1;
    }
}

static void handle_list_key(ui_window_t *win, ui_widget_t *wg, int idx, char key) {
    int visible = wg->h / ui_theme.row_height;

    if (key == KEY_UP && wg->list.selected > 0) {
        wg->list.selected--;
        if (wg->list.selected < wg->list.scroll)
            wg->list.scroll = wg->list.selected;
        if (wg->list.on_select) wg->list.on_select(win, idx);
        win->dirty = 1;
    }
    if (key == KEY_DOWN && wg->list.selected < wg->list.count - 1) {
        wg->list.selected++;
        if (wg->list.selected >= wg->list.scroll + visible)
            wg->list.scroll = wg->list.selected - visible + 1;
        if (wg->list.on_select) wg->list.on_select(win, idx);
        win->dirty = 1;
    }
    if (key == '\n') {
        if (wg->list.on_activate) wg->list.on_activate(win, idx);
        win->dirty = 1;
    }
}

void ui_dispatch_event(ui_window_t *win, ui_event_t *ev) {
    if (!win || !ev) return;

    if (ev->type == UI_EVENT_KEY_PRESS) {
        char key = ev->key.key;

        /* Tab navigation */
        if (key == '\t') {
            ui_focus_next(win);
            return;
        }

        /* If a widget is capturing keys, send to it */
        if (win->focused_widget >= 0) {
            ui_widget_t *wg = &win->widgets[win->focused_widget];
            int idx = win->focused_widget;

            if (wg->type == UI_TEXTINPUT && (wg->flags & UI_FLAG_CAPTURING)) {
                handle_textinput_key(win, wg, idx, key);
                return;
            }

            /* Focus-specific key handling */
            switch (wg->type) {
            case UI_BUTTON:
                if (key == '\n' || key == ' ') {
                    if (wg->button.on_click) wg->button.on_click(win, idx);
                    win->dirty = 1;
                }
                break;
            case UI_TEXTINPUT:
                /* Start capturing on Enter or any printable char */
                if (key == '\n' || (key >= 32 && key < 127)) {
                    wg->flags |= UI_FLAG_CAPTURING;
                    if (key >= 32 && key < 127)
                        handle_textinput_key(win, wg, idx, key);
                    win->dirty = 1;
                }
                break;
            case UI_LIST:
                handle_list_key(win, wg, idx, key);
                break;
            case UI_CHECKBOX:
                if (key == '\n' || key == ' ') {
                    wg->checkbox.checked = !wg->checkbox.checked;
                    if (wg->checkbox.on_change) wg->checkbox.on_change(win, idx);
                    win->dirty = 1;
                }
                break;
            case UI_TABS:
                if (key >= '1' && key <= '9') {
                    int t = key - '1';
                    if (t < wg->tabs.count) {
                        wg->tabs.active = t;
                        if (wg->tabs.on_change) wg->tabs.on_change(win, idx);
                        win->dirty = 1;
                    }
                }
                if (key == KEY_LEFT && wg->tabs.active > 0) {
                    wg->tabs.active--;
                    if (wg->tabs.on_change) wg->tabs.on_change(win, idx);
                    win->dirty = 1;
                }
                if (key == KEY_RIGHT && wg->tabs.active < wg->tabs.count - 1) {
                    wg->tabs.active++;
                    if (wg->tabs.on_change) wg->tabs.on_change(win, idx);
                    win->dirty = 1;
                }
                break;
            case UI_CUSTOM:
                if (wg->custom.event) {
                    if (wg->custom.event(win, idx, ev))
                        win->dirty = 1;
                }
                break;
            case UI_TOGGLE:
                if (key == '\n' || key == ' ') {
                    wg->toggle.on = !wg->toggle.on;
                    if (wg->toggle.on_change) wg->toggle.on_change(win, idx);
                    win->dirty = 1;
                }
                break;
            case UI_ICON_GRID: {
                int cols = wg->icon_grid.cols > 0 ? wg->icon_grid.cols : 4;
                if (key == KEY_LEFT && wg->icon_grid.selected > 0) {
                    wg->icon_grid.selected--;
                    win->dirty = 1;
                }
                if (key == KEY_RIGHT && wg->icon_grid.selected < wg->icon_grid.count - 1) {
                    wg->icon_grid.selected++;
                    win->dirty = 1;
                }
                if (key == KEY_UP && wg->icon_grid.selected >= cols) {
                    wg->icon_grid.selected -= cols;
                    win->dirty = 1;
                }
                if (key == KEY_DOWN && wg->icon_grid.selected + cols < wg->icon_grid.count) {
                    wg->icon_grid.selected += cols;
                    win->dirty = 1;
                }
                if (key == '\n' && wg->icon_grid.on_activate) {
                    wg->icon_grid.on_activate(win, idx);
                    win->dirty = 1;
                }
                break;
            }
            }
        }
    }

    /* Mouse events: hit test to find widget */
    if (ev->type == UI_EVENT_MOUSE_DOWN || ev->type == UI_EVENT_MOUSE_UP) {
        /* Convert screen coords to window-relative */
        wm_window_t *wmw = wm_get_window(win->wm_id);
        if (!wmw) return;
        int wx = ev->mouse.x - wmw->x - WM_BORDER;
        int wy = ev->mouse.y - wmw->y - WM_TITLEBAR_H;

        for (int i = win->widget_count - 1; i >= 0; i--) {
            ui_widget_t *wg = &win->widgets[i];
            if (!(wg->flags & UI_FLAG_VISIBLE)) continue;
            if (!(wg->flags & UI_FLAG_FOCUSABLE)) continue;
            if (!point_in_widget(wg, wx, wy)) continue;

            /* Focus this widget */
            if (win->focused_widget >= 0 && win->focused_widget != i)
                win->widgets[win->focused_widget].flags &= ~UI_FLAG_CAPTURING;
            win->focused_widget = i;
            win->dirty = 1;

            if (ev->type == UI_EVENT_MOUSE_DOWN) {
                switch (wg->type) {
                case UI_BUTTON:
                    wg->button.pressed = 1;
                    break;
                case UI_TEXTINPUT:
                    wg->flags |= UI_FLAG_CAPTURING;
                    break;
                case UI_LIST: {
                    int row = (wy - wg->y) / ui_theme.row_height;
                    int idx = wg->list.scroll + row;
                    if (idx >= 0 && idx < wg->list.count) {
                        wg->list.selected = idx;
                        if (wg->list.on_select) wg->list.on_select(win, i);
                    }
                    break;
                }
                case UI_CHECKBOX:
                    wg->checkbox.checked = !wg->checkbox.checked;
                    if (wg->checkbox.on_change) wg->checkbox.on_change(win, i);
                    break;
                case UI_TABS: {
                    int tab_w = wg->w / (wg->tabs.count > 0 ? wg->tabs.count : 1);
                    int t = (wx - wg->x) / tab_w;
                    if (t >= 0 && t < wg->tabs.count && t != wg->tabs.active) {
                        wg->tabs.active = t;
                        if (wg->tabs.on_change) wg->tabs.on_change(win, i);
                    }
                    break;
                }
                case UI_CUSTOM:
                    if (wg->custom.event) {
                        ui_event_t local = *ev;
                        local.mouse.wx = wx;
                        local.mouse.wy = wy;
                        if (wg->custom.event(win, i, &local))
                            win->dirty = 1;
                    }
                    break;
                case UI_TOGGLE:
                    wg->toggle.on = !wg->toggle.on;
                    if (wg->toggle.on_change) wg->toggle.on_change(win, i);
                    break;
                case UI_ICON_GRID: {
                    int col = (wx - wg->x) / wg->icon_grid.cell_w;
                    int row = (wy - wg->y + wg->icon_grid.scroll) / wg->icon_grid.cell_h;
                    int cols = wg->icon_grid.cols > 0 ? wg->icon_grid.cols : 4;
                    int clicked = row * cols + col;
                    if (clicked >= 0 && clicked < wg->icon_grid.count) {
                        wg->icon_grid.selected = clicked;
                        if (wg->icon_grid.on_activate)
                            wg->icon_grid.on_activate(win, i);
                    }
                    break;
                }
                }
            }
            if (ev->type == UI_EVENT_MOUSE_UP) {
                if (wg->type == UI_BUTTON && wg->button.pressed) {
                    wg->button.pressed = 0;
                    if (wg->button.on_click) wg->button.on_click(win, i);
                }
            }
            return;
        }
    }
}

/* ═══ Resize detection ═══════════════════════════════════════════ */

void ui_window_check_resize(ui_window_t *win) {
    if (!win) return;
    int cur_cw = 0, cur_ch = 0;
    wm_get_canvas(win->wm_id, &cur_cw, &cur_ch);
    if (cur_cw != win->prev_cw || cur_ch != win->prev_ch) {
        for (int i = 0; i < win->widget_count; i++) {
            ui_widget_t *wg = &win->widgets[i];
            if (wg->w == win->prev_cw) wg->w = cur_cw;
            if (wg->h + wg->y == win->prev_ch) wg->h += (cur_ch - win->prev_ch);
            else if (wg->y > win->prev_ch / 2 && wg->h + wg->y <= win->prev_ch)
                wg->y += (cur_ch - win->prev_ch);
        }
        win->prev_cw = cur_cw;
        win->prev_ch = cur_ch;
        win->dirty = 1;
    }
}

/* ═══ App run loop ═══════════════════════════════════════════════ */

int ui_app_run(ui_window_t *win, void (*on_event)(ui_window_t *, ui_event_t *)) {
    if (!win) return 0;

    ui_event_init();
    keyboard_set_idle_callback(ui_idle_handler);

    /* Auto-focus first focusable widget */
    if (win->focused_widget < 0)
        ui_focus_next(win);

    while (1) {
        /* Detect canvas resize and adapt widgets */
        ui_window_check_resize(win);

        if (win->dirty) {
            ui_window_redraw(win);
            wm_composite();
        }

        ui_event_t ev;
        ui_poll_event(&ev);

        if (ev.type == UI_EVENT_CLOSE) break;
        if (ev.type == UI_EVENT_DOCK) {
            keyboard_set_idle_callback(0);
            return ev.dock.action;
        }

        /* Alt+Tab */
        if (ev.type == UI_EVENT_KEY_PRESS && ev.key.key == KEY_ALT_TAB) {
            wm_cycle_focus();
            continue;
        }
        /* Super key — return to desktop */
        if (ev.type == UI_EVENT_KEY_PRESS && ev.key.key == KEY_SUPER) break;
        /* Escape — close */
        if (ev.type == UI_EVENT_KEY_PRESS && ev.key.key == KEY_ESCAPE) break;

        /* App-specific handling */
        if (on_event) on_event(win, &ev);

        /* Widget dispatch */
        ui_dispatch_event(win, &ev);
    }

    keyboard_set_idle_callback(0);
    return 0;
}
