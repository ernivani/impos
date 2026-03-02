/* ui_widget.c — Widget toolkit implementation (Phase 7.5)
 *
 * Retained-mode widget system: widgets stored in flat array per window,
 * framework handles drawing and event dispatch.
 *
 * Pool allocator for ui_window_t (8 slots). Each window owns up to
 * UI_MAX_WIDGETS (48) widgets drawn via gfx_surf_* primitives.
 */
#include <kernel/ui_widget.h>
#include <kernel/ui_window.h>
#include <kernel/ui_theme.h>
#include <kernel/ui_event.h>
#include <kernel/clipboard.h>
#include <kernel/gfx.h>
#include <string.h>
#include <stdio.h>

/* ── Window pool ───────────────────────────────────────────────── */

#define UW_MAX_WINDOWS 8

static ui_window_t uw_pool[UW_MAX_WINDOWS];
static int uw_used[UW_MAX_WINDOWS];

ui_window_t *uw_create(int x, int y, int w, int h, const char *title)
{
    int i;
    for (i = 0; i < UW_MAX_WINDOWS; i++) {
        if (!uw_used[i]) {
            uw_used[i] = 1;
            ui_window_t *win = &uw_pool[i];
            memset(win, 0, sizeof(*win));
            win->wm_id = ui_window_create(x, y, w, h, title);
            win->widget_count = 0;
            win->focused_widget = -1;
            win->dirty = 1;
            win->app_data = NULL;
            win->prev_cw = 0;
            win->prev_ch = 0;
            return win;
        }
    }
    return NULL;
}

void uw_destroy(ui_window_t *win)
{
    if (!win) return;
    int i;
    for (i = 0; i < UW_MAX_WINDOWS; i++) {
        if (uw_used[i] && &uw_pool[i] == win) {
            if (win->wm_id >= 0)
                ui_window_close_animated(win->wm_id);
            win->wm_id = -1;
            uw_used[i] = 0;
            return;
        }
    }
}

/* ── Widget add helpers ────────────────────────────────────────── */

static int add_widget(ui_window_t *win, int type, int x, int y, int w, int h,
                      uint16_t extra_flags)
{
    if (!win || win->widget_count >= UI_MAX_WIDGETS) return -1;
    int idx = win->widget_count++;
    ui_widget_t *wg = &win->widgets[idx];
    memset(wg, 0, sizeof(*wg));
    wg->type = type;
    wg->x = x; wg->y = y; wg->w = w; wg->h = h;
    wg->flags = UI_FLAG_VISIBLE | extra_flags;
    wg->parent = -1;
    win->dirty = 1;
    return idx;
}

int ui_add_label(ui_window_t *win, int x, int y, int w, int h,
                 const char *text, uint32_t color)
{
    int idx = add_widget(win, UI_LABEL, x, y, w, h, 0);
    if (idx < 0) return -1;
    ui_widget_t *wg = &win->widgets[idx];
    if (text) strncpy(wg->label.text, text, UI_TEXT_MAX - 1);
    wg->label.color = color;
    return idx;
}

int ui_add_button(ui_window_t *win, int x, int y, int w, int h,
                  const char *text, ui_callback_t on_click)
{
    int idx = add_widget(win, UI_BUTTON, x, y, w, h, UI_FLAG_FOCUSABLE);
    if (idx < 0) return -1;
    ui_widget_t *wg = &win->widgets[idx];
    if (text) strncpy(wg->button.text, text, sizeof(wg->button.text) - 1);
    wg->button.on_click = on_click;
    return idx;
}

int ui_add_textinput(ui_window_t *win, int x, int y, int w, int h,
                     const char *placeholder, int max_len, int is_password)
{
    int idx = add_widget(win, UI_TEXTINPUT, x, y, w, h, UI_FLAG_FOCUSABLE);
    if (idx < 0) return -1;
    ui_widget_t *wg = &win->widgets[idx];
    if (placeholder)
        strncpy(wg->textinput.placeholder, placeholder,
                sizeof(wg->textinput.placeholder) - 1);
    wg->textinput.max_len = (max_len > 0 && max_len < UI_TEXT_MAX)
                            ? max_len : UI_TEXT_MAX - 1;
    wg->textinput.password = is_password ? 1 : 0;
    wg->textinput.cursor = 0;
    wg->textinput.sel_start = -1;
    return idx;
}

int ui_add_list(ui_window_t *win, int x, int y, int w, int h,
                const char **items, int count)
{
    int idx = add_widget(win, UI_LIST, x, y, w, h, UI_FLAG_FOCUSABLE);
    if (idx < 0) return -1;
    ui_widget_t *wg = &win->widgets[idx];
    wg->list.items = items;
    wg->list.count = count;
    wg->list.selected = -1;
    wg->list.scroll = 0;
    return idx;
}

int ui_add_checkbox(ui_window_t *win, int x, int y, int w, int h,
                    const char *text, int checked)
{
    int idx = add_widget(win, UI_CHECKBOX, x, y, w, h, UI_FLAG_FOCUSABLE);
    if (idx < 0) return -1;
    ui_widget_t *wg = &win->widgets[idx];
    if (text) strncpy(wg->checkbox.text, text, sizeof(wg->checkbox.text) - 1);
    wg->checkbox.checked = checked ? 1 : 0;
    return idx;
}

int ui_add_progress(ui_window_t *win, int x, int y, int w, int h,
                    int value, const char *label)
{
    int idx = add_widget(win, UI_PROGRESS, x, y, w, h, 0);
    if (idx < 0) return -1;
    ui_widget_t *wg = &win->widgets[idx];
    wg->progress.value = value;
    if (label) strncpy(wg->progress.label, label,
                       sizeof(wg->progress.label) - 1);
    return idx;
}

int ui_add_tabs(ui_window_t *win, int x, int y, int w, int h,
                const char **labels, int count)
{
    int idx = add_widget(win, UI_TABS, x, y, w, h, UI_FLAG_FOCUSABLE);
    if (idx < 0) return -1;
    ui_widget_t *wg = &win->widgets[idx];
    wg->tabs.labels = labels;
    wg->tabs.count = count;
    wg->tabs.active = 0;
    return idx;
}

int ui_add_panel(ui_window_t *win, int x, int y, int w, int h,
                 const char *title)
{
    int idx = add_widget(win, UI_PANEL, x, y, w, h, 0);
    if (idx < 0) return -1;
    ui_widget_t *wg = &win->widgets[idx];
    if (title) strncpy(wg->panel.title, title,
                       sizeof(wg->panel.title) - 1);
    return idx;
}

int ui_add_separator(ui_window_t *win, int x, int y, int w)
{
    return add_widget(win, UI_SEPARATOR, x, y, w, 1, 0);
}

int ui_add_custom(ui_window_t *win, int x, int y, int w, int h,
                  ui_custom_draw_t draw_fn, ui_custom_event_t event_fn,
                  void *userdata)
{
    int idx = add_widget(win, UI_CUSTOM, x, y, w, h, UI_FLAG_FOCUSABLE);
    if (idx < 0) return -1;
    ui_widget_t *wg = &win->widgets[idx];
    wg->custom.draw = draw_fn;
    wg->custom.event = event_fn;
    wg->custom.userdata = userdata;
    return idx;
}

int ui_add_toggle(ui_window_t *win, int x, int y, int w, int h,
                  const char *text, int on)
{
    int idx = add_widget(win, UI_TOGGLE, x, y, w, h, UI_FLAG_FOCUSABLE);
    if (idx < 0) return -1;
    ui_widget_t *wg = &win->widgets[idx];
    if (text) strncpy(wg->toggle.text, text, sizeof(wg->toggle.text) - 1);
    wg->toggle.on = on ? 1 : 0;
    return idx;
}

int ui_add_icon_grid(ui_window_t *win, int x, int y, int w, int h,
                     int cols, int cell_w, int cell_h,
                     const char **labels, int count,
                     void (*draw_icon)(int idx, int x, int y, int sel))
{
    int idx = add_widget(win, UI_ICON_GRID, x, y, w, h, UI_FLAG_FOCUSABLE);
    if (idx < 0) return -1;
    ui_widget_t *wg = &win->widgets[idx];
    wg->icon_grid.cols = cols;
    wg->icon_grid.cell_w = cell_w;
    wg->icon_grid.cell_h = cell_h;
    wg->icon_grid.labels = labels;
    wg->icon_grid.count = count;
    wg->icon_grid.selected = -1;
    return idx;
}

int ui_add_card(ui_window_t *win, int x, int y, int w, int h,
                const char *title, uint32_t bg_color, int radius)
{
    int idx = add_widget(win, UI_CARD, x, y, w, h, 0);
    if (idx < 0) return -1;
    ui_widget_t *wg = &win->widgets[idx];
    if (title) strncpy(wg->card.title, title, sizeof(wg->card.title) - 1);
    wg->card.bg_color = bg_color;
    wg->card.radius = radius;
    return idx;
}

/* ── Widget access ─────────────────────────────────────────────── */

ui_widget_t *ui_get_widget(ui_window_t *win, int idx)
{
    if (!win || idx < 0 || idx >= win->widget_count) return NULL;
    return &win->widgets[idx];
}

void ui_widget_set_visible(ui_window_t *win, int idx, int visible)
{
    if (!win || idx < 0 || idx >= win->widget_count) return;
    if (visible)
        win->widgets[idx].flags |= UI_FLAG_VISIBLE;
    else
        win->widgets[idx].flags &= ~UI_FLAG_VISIBLE;
    win->dirty = 1;
}

void ui_widget_set_visible_range(ui_window_t *win, int from, int to,
                                 int visible)
{
    int i;
    if (!win) return;
    for (i = from; i <= to && i < win->widget_count; i++)
        ui_widget_set_visible(win, i, visible);
}

/* ── Drawing helpers ───────────────────────────────────────────── */

static void draw_label(gfx_surface_t *gs, ui_widget_t *w)
{
    uint32_t col = w->label.color ? w->label.color : ui_theme.text_primary;
    gfx_surf_draw_string_smooth(gs, w->x, w->y + 2, w->label.text, col, 1);
}

static void draw_button(gfx_surface_t *gs, ui_widget_t *w, int focused)
{
    uint32_t bg, fg;
    if (w->button.primary) {
        bg = w->button.pressed ? ui_theme.btn_primary_hover
                               : ui_theme.btn_primary_bg;
        fg = ui_theme.btn_primary_text;
    } else {
        bg = w->button.pressed ? ui_theme.btn_pressed
           : (w->flags & UI_FLAG_HOVER) ? ui_theme.btn_hover
           : ui_theme.btn_bg;
        fg = ui_theme.btn_text;
    }
    gfx_surf_rounded_rect(gs, w->x, w->y, w->w, w->h, 4, bg);
    if (focused)
        gfx_surf_rounded_rect_outline(gs, w->x, w->y, w->w, w->h, 4,
                                      ui_theme.accent);

    /* Center text */
    int tw = (int)strlen(w->button.text) * 8;
    int tx = w->x + (w->w - tw) / 2;
    int ty = w->y + (w->h - 16) / 2;
    gfx_surf_draw_string_smooth(gs, tx, ty, w->button.text, fg, 1);
}

static void draw_textinput(gfx_surface_t *gs, ui_widget_t *w, int focused)
{
    uint32_t bg = ui_theme.input_bg;
    uint32_t border = focused ? ui_theme.input_border_focus
                              : ui_theme.input_border;

    gfx_surf_rounded_rect(gs, w->x, w->y, w->w, w->h, 4, bg);
    gfx_surf_rounded_rect_outline(gs, w->x, w->y, w->w, w->h, 4, border);

    int tx = w->x + 6;
    int ty = w->y + (w->h - 16) / 2;
    int max_chars = (w->w - 12) / 8;

    if (w->textinput.text[0]) {
        /* Scroll so cursor is visible */
        int len = (int)strlen(w->textinput.text);
        int scroll = w->textinput.scroll;
        if (w->textinput.cursor - scroll >= max_chars)
            scroll = w->textinput.cursor - max_chars + 1;
        if (w->textinput.cursor < scroll)
            scroll = w->textinput.cursor;
        if (scroll < 0) scroll = 0;
        w->textinput.scroll = scroll;

        /* Draw visible portion */
        char vis[UI_TEXT_MAX];
        int vlen = len - scroll;
        if (vlen > max_chars) vlen = max_chars;
        if (vlen > 0) {
            memcpy(vis, w->textinput.text + scroll, (size_t)vlen);
            vis[vlen] = '\0';
            if (w->textinput.password) {
                int j;
                for (j = 0; j < vlen; j++) vis[j] = '*';
            }
            gfx_surf_draw_string_smooth(gs, tx, ty, vis,
                                        ui_theme.text_primary, 1);
        }

        /* Cursor */
        if (focused) {
            int cx = tx + (w->textinput.cursor - scroll) * 8;
            gfx_surf_fill_rect(gs, cx, w->y + 4, 1, w->h - 8,
                               ui_theme.text_primary);
        }
    } else if (w->textinput.placeholder[0]) {
        gfx_surf_draw_string_smooth(gs, tx, ty, w->textinput.placeholder,
                                    ui_theme.input_placeholder, 1);
        if (focused)
            gfx_surf_fill_rect(gs, tx, w->y + 4, 1, w->h - 8,
                               ui_theme.text_primary);
    }
}

static void draw_list(gfx_surface_t *gs, ui_widget_t *w)
{
    int row_h = 20;
    int visible = w->h / row_h;
    int i;
    for (i = 0; i < visible && (i + w->list.scroll) < w->list.count; i++) {
        int idx = i + w->list.scroll;
        int ry = w->y + i * row_h;
        if (idx == w->list.selected)
            gfx_surf_fill_rect(gs, w->x, ry, w->w, row_h,
                               ui_theme.list_sel_bg);
        else if (idx % 2)
            gfx_surf_fill_rect(gs, w->x, ry, w->w, row_h,
                               ui_theme.win_body_bg);
        if (w->list.items && w->list.items[idx])
            gfx_surf_draw_string_smooth(gs, w->x + 4, ry + 2,
                                        w->list.items[idx],
                                        ui_theme.text_primary, 1);
    }
}

static void draw_checkbox(gfx_surface_t *gs, ui_widget_t *w)
{
    int bx = w->x, by = w->y + (w->h - 14) / 2;
    gfx_surf_draw_rect(gs, bx, by, 14, 14, ui_theme.checkbox_border);
    if (w->checkbox.checked) {
        gfx_surf_fill_rect(gs, bx + 2, by + 2, 10, 10,
                           ui_theme.checkbox_checked);
        /* Check mark: two diagonal lines */
        gfx_surf_draw_line(gs, bx + 3, by + 7, bx + 5, by + 10,
                           ui_theme.checkbox_check);
        gfx_surf_draw_line(gs, bx + 5, by + 10, bx + 11, by + 3,
                           ui_theme.checkbox_check);
    }
    if (w->checkbox.text[0])
        gfx_surf_draw_string_smooth(gs, w->x + 20, w->y + (w->h - 16) / 2,
                                    w->checkbox.text, ui_theme.text_primary,
                                    1);
}

static void draw_progress(gfx_surface_t *gs, ui_widget_t *w)
{
    int bar_h = w->h > 20 ? 12 : w->h - 4;
    int bar_y = w->y + (w->h - bar_h) / 2;
    int label_w = 0;

    if (w->progress.label[0]) {
        gfx_surf_draw_string_smooth(gs, w->x, w->y, w->progress.label,
                                    ui_theme.text_dim, 1);
        label_w = 0; /* label on top, bar full width */
        bar_y = w->y + 18;
        bar_h = w->h - 22;
        if (bar_h < 6) bar_h = 6;
    }

    int bar_w = w->w - label_w;
    gfx_surf_rounded_rect(gs, w->x + label_w, bar_y, bar_w, bar_h, 3,
                          ui_theme.progress_bg);
    int val = w->progress.value;
    if (val > 100) val = 100;
    if (val > 0) {
        int fw = bar_w * val / 100;
        if (fw < 6) fw = 6;
        uint32_t fill = (val > 90) ? ui_theme.progress_warn
                                   : ui_theme.progress_fill;
        gfx_surf_rounded_rect(gs, w->x + label_w, bar_y, fw, bar_h, 3,
                              fill);
    }
}

static void draw_tabs(gfx_surface_t *gs, ui_widget_t *w)
{
    if (!w->tabs.labels || w->tabs.count <= 0) return;
    int tab_w = w->w / w->tabs.count;
    int i;
    for (i = 0; i < w->tabs.count; i++) {
        int tx = w->x + i * tab_w;
        uint32_t bg = (i == w->tabs.active) ? ui_theme.tab_active_bg
                                            : ui_theme.tab_bg;
        uint32_t fg = (i == w->tabs.active) ? ui_theme.tab_active_text
                                            : ui_theme.tab_text;
        gfx_surf_fill_rect(gs, tx, w->y, tab_w, w->h, bg);
        if (i == w->tabs.active)
            gfx_surf_fill_rect(gs, tx, w->y + w->h - 2, tab_w, 2,
                               ui_theme.accent);
        if (w->tabs.labels[i]) {
            int tw = (int)strlen(w->tabs.labels[i]) * 8;
            gfx_surf_draw_string_smooth(gs, tx + (tab_w - tw) / 2,
                                        w->y + (w->h - 16) / 2,
                                        w->tabs.labels[i], fg, 1);
        }
    }
}

static void draw_panel(gfx_surface_t *gs, ui_widget_t *w)
{
    gfx_surf_draw_rect(gs, w->x, w->y, w->w, w->h, ui_theme.border);
    if (w->panel.title[0])
        gfx_surf_draw_string_smooth(gs, w->x + 6, w->y + 4,
                                    w->panel.title, ui_theme.text_dim, 1);
}

static void draw_separator(gfx_surface_t *gs, ui_widget_t *w)
{
    gfx_surf_fill_rect(gs, w->x, w->y, w->w, 1, ui_theme.border);
}

static void draw_toggle(gfx_surface_t *gs, ui_widget_t *w)
{
    int track_w = 36, track_h = 18;
    int ty = w->y + (w->h - track_h) / 2;
    uint32_t track_col = w->toggle.on ? ui_theme.toggle_on_bg
                                      : ui_theme.toggle_off_bg;
    gfx_surf_rounded_rect(gs, w->x, ty, track_w, track_h, 9, track_col);

    /* Knob */
    int knob_r = 6;
    int kx = w->toggle.on ? w->x + track_w - knob_r - 4
                           : w->x + knob_r + 4;
    int ky = ty + track_h / 2;
    gfx_surf_fill_circle(gs, kx, ky, knob_r, ui_theme.toggle_handle);

    if (w->toggle.text[0])
        gfx_surf_draw_string_smooth(gs, w->x + track_w + 8,
                                    w->y + (w->h - 16) / 2,
                                    w->toggle.text, ui_theme.text_primary,
                                    1);
}

static void draw_card(gfx_surface_t *gs, ui_widget_t *w)
{
    uint32_t bg = w->card.bg_color ? w->card.bg_color : ui_theme.card_bg;
    int r = w->card.radius > 0 ? w->card.radius : 6;
    gfx_surf_rounded_rect(gs, w->x, w->y, w->w, w->h, r, bg);
    gfx_surf_rounded_rect_outline(gs, w->x, w->y, w->w, w->h, r,
                                  ui_theme.card_border);
    if (w->card.title[0])
        gfx_surf_draw_string_smooth(gs, w->x + 8, w->y + 6,
                                    w->card.title, ui_theme.text_primary, 1);
}

/* ── Rendering ─────────────────────────────────────────────────── */

void uw_redraw(ui_window_t *win)
{
    if (!win || win->wm_id < 0) return;
    int cw, ch;
    uint32_t *canvas = ui_window_canvas(win->wm_id, &cw, &ch);
    if (!canvas) return;

    gfx_surface_t gs = { canvas, cw, ch, cw };

    /* Clear background */
    gfx_surf_fill_rect(&gs, 0, 0, cw, ch, ui_theme.win_body_bg);

    /* Draw each visible widget */
    int i;
    for (i = 0; i < win->widget_count; i++) {
        ui_widget_t *w = &win->widgets[i];
        if (!(w->flags & UI_FLAG_VISIBLE)) continue;
        int focused = (i == win->focused_widget);

        switch (w->type) {
        case UI_LABEL:      draw_label(&gs, w); break;
        case UI_BUTTON:     draw_button(&gs, w, focused); break;
        case UI_TEXTINPUT:  draw_textinput(&gs, w, focused); break;
        case UI_LIST:       draw_list(&gs, w); break;
        case UI_CHECKBOX:   draw_checkbox(&gs, w); break;
        case UI_PROGRESS:   draw_progress(&gs, w); break;
        case UI_TABS:       draw_tabs(&gs, w); break;
        case UI_PANEL:      draw_panel(&gs, w); break;
        case UI_SEPARATOR:  draw_separator(&gs, w); break;
        case UI_TOGGLE:     draw_toggle(&gs, w); break;
        case UI_CARD:       draw_card(&gs, w); break;
        case UI_CUSTOM:
            if (w->custom.draw)
                w->custom.draw(win, i, canvas, cw, ch);
            break;
        default: break;
        }
    }

    ui_window_damage_all(win->wm_id);
    win->dirty = 0;
}

void ui_window_check_resize(ui_window_t *win)
{
    if (!win || win->wm_id < 0) return;
    int cw, ch;
    ui_window_canvas(win->wm_id, &cw, &ch);
    if (cw != win->prev_cw || ch != win->prev_ch) {
        win->prev_cw = cw;
        win->prev_ch = ch;
        win->dirty = 1;
    }
}

/* ── Focus management ──────────────────────────────────────────── */

void ui_focus_next(ui_window_t *win)
{
    if (!win || win->widget_count == 0) return;
    int start = win->focused_widget + 1;
    int i;
    for (i = 0; i < win->widget_count; i++) {
        int idx = (start + i) % win->widget_count;
        ui_widget_t *w = &win->widgets[idx];
        if ((w->flags & UI_FLAG_VISIBLE) && (w->flags & UI_FLAG_FOCUSABLE)
            && !(w->flags & UI_FLAG_DISABLED)) {
            win->focused_widget = idx;
            win->dirty = 1;
            return;
        }
    }
}

void ui_focus_prev(ui_window_t *win)
{
    if (!win || win->widget_count == 0) return;
    int start = win->focused_widget - 1;
    if (start < 0) start = win->widget_count - 1;
    int i;
    for (i = 0; i < win->widget_count; i++) {
        int idx = (start - i + win->widget_count) % win->widget_count;
        ui_widget_t *w = &win->widgets[idx];
        if ((w->flags & UI_FLAG_VISIBLE) && (w->flags & UI_FLAG_FOCUSABLE)
            && !(w->flags & UI_FLAG_DISABLED)) {
            win->focused_widget = idx;
            win->dirty = 1;
            return;
        }
    }
}

/* ── Event dispatch ────────────────────────────────────────────── */

static int hit_test_widget(ui_window_t *win, int lx, int ly)
{
    int i;
    /* Reverse order: last widget = top */
    for (i = win->widget_count - 1; i >= 0; i--) {
        ui_widget_t *w = &win->widgets[i];
        if (!(w->flags & UI_FLAG_VISIBLE)) continue;
        if (lx >= w->x && lx < w->x + w->w &&
            ly >= w->y && ly < w->y + w->h)
            return i;
    }
    return -1;
}

void ui_dispatch_event(ui_window_t *win, ui_event_t *ev)
{
    if (!win || !ev) return;

    if (ev->type == UI_EVENT_MOUSE_DOWN) {
        int hit = hit_test_widget(win, ev->mouse.wx, ev->mouse.wy);

        /* Clear hover on all */
        int i;
        for (i = 0; i < win->widget_count; i++)
            win->widgets[i].flags &= ~UI_FLAG_HOVER;

        if (hit >= 0) {
            ui_widget_t *w = &win->widgets[hit];
            if (w->flags & UI_FLAG_FOCUSABLE)
                win->focused_widget = hit;

            switch (w->type) {
            case UI_BUTTON:
                w->button.pressed = 1;
                break;
            case UI_CHECKBOX:
                w->checkbox.checked = !w->checkbox.checked;
                if (w->checkbox.on_change)
                    w->checkbox.on_change(win, hit);
                break;
            case UI_TOGGLE:
                w->toggle.on = !w->toggle.on;
                if (w->toggle.on_change)
                    w->toggle.on_change(win, hit);
                break;
            case UI_LIST: {
                int row_h = 20;
                int row = (ev->mouse.wy - w->y) / row_h + w->list.scroll;
                if (row >= 0 && row < w->list.count) {
                    w->list.selected = row;
                    if (w->list.on_select)
                        w->list.on_select(win, hit);
                }
                break;
            }
            case UI_TABS: {
                if (w->tabs.count > 0) {
                    int tab_w = w->w / w->tabs.count;
                    int tab = (ev->mouse.wx - w->x) / tab_w;
                    if (tab >= 0 && tab < w->tabs.count &&
                        tab != w->tabs.active) {
                        w->tabs.active = tab;
                        if (w->tabs.on_change)
                            w->tabs.on_change(win, hit);
                    }
                }
                break;
            }
            case UI_TEXTINPUT: {
                int scroll = w->textinput.scroll;
                int click_pos = (ev->mouse.wx - w->x - 6) / 8 + scroll;
                int len = (int)strlen(w->textinput.text);
                if (click_pos < 0) click_pos = 0;
                if (click_pos > len) click_pos = len;
                w->textinput.cursor = click_pos;
                w->textinput.sel_start = -1;
                break;
            }
            case UI_CUSTOM:
                if (w->custom.event)
                    w->custom.event(win, hit, ev);
                break;
            default:
                break;
            }
        }
        win->dirty = 1;
    }

    if (ev->type == UI_EVENT_MOUSE_UP) {
        int hit = hit_test_widget(win, ev->mouse.wx, ev->mouse.wy);
        int i;
        for (i = 0; i < win->widget_count; i++) {
            ui_widget_t *w = &win->widgets[i];
            if (w->type == UI_BUTTON && w->button.pressed) {
                w->button.pressed = 0;
                if (i == hit && w->button.on_click)
                    w->button.on_click(win, i);
            }
        }
        win->dirty = 1;
    }

    if (ev->type == UI_EVENT_MOUSE_MOVE) {
        int hit = hit_test_widget(win, ev->mouse.wx, ev->mouse.wy);
        int i;
        for (i = 0; i < win->widget_count; i++) {
            if (i == hit)
                win->widgets[i].flags |= UI_FLAG_HOVER;
            else
                win->widgets[i].flags &= ~UI_FLAG_HOVER;
        }
        win->dirty = 1;

        if (hit >= 0 && win->widgets[hit].type == UI_CUSTOM) {
            ui_widget_t *w = &win->widgets[hit];
            if (w->custom.event)
                w->custom.event(win, hit, ev);
        }
    }

    if (ev->type == UI_EVENT_KEY_PRESS) {
        int fi = win->focused_widget;

        /* Tab key: cycle focus */
        if (ev->key.key == '\t') {
            if (ev->key.shift)
                ui_focus_prev(win);
            else
                ui_focus_next(win);
            win->dirty = 1;
            return;
        }

        if (fi >= 0 && fi < win->widget_count) {
            ui_widget_t *w = &win->widgets[fi];

            if (w->type == UI_TEXTINPUT) {
                char ch = ev->key.key;
                int len = (int)strlen(w->textinput.text);

                /* Ctrl+C/V/X/A: clipboard operations */
                if (ev->key.ctrl && (ch == 'c' || ch == 'v' || ch == 'x' || ch == 'a')) {
                    if (ch == 'c') {
                        /* Copy: selected text or all text */
                        if (w->textinput.sel_start >= 0 &&
                            w->textinput.sel_start != w->textinput.cursor) {
                            int s = w->textinput.sel_start < w->textinput.cursor
                                    ? w->textinput.sel_start : w->textinput.cursor;
                            int e = w->textinput.sel_start > w->textinput.cursor
                                    ? w->textinput.sel_start : w->textinput.cursor;
                            clipboard_copy(w->textinput.text + s, (size_t)(e - s));
                        } else {
                            clipboard_copy(w->textinput.text, (size_t)len);
                        }
                    } else if (ch == 'v') {
                        /* Paste: insert clipboard text at cursor */
                        size_t clip_len;
                        const char *clip = clipboard_get(&clip_len);
                        if (clip && clip_len > 0) {
                            int space = w->textinput.max_len - len;
                            if (space < 0) space = 0;
                            int insert = (int)clip_len < space ? (int)clip_len : space;
                            if (insert > 0) {
                                memmove(w->textinput.text + w->textinput.cursor + insert,
                                        w->textinput.text + w->textinput.cursor,
                                        (size_t)(len - w->textinput.cursor + 1));
                                memcpy(w->textinput.text + w->textinput.cursor, clip, (size_t)insert);
                                w->textinput.cursor += insert;
                            }
                        }
                    } else if (ch == 'x') {
                        /* Cut: copy all text, then clear */
                        clipboard_copy(w->textinput.text, (size_t)len);
                        w->textinput.text[0] = '\0';
                        w->textinput.cursor = 0;
                        w->textinput.sel_start = -1;
                    } else if (ch == 'a') {
                        /* Select all */
                        w->textinput.sel_start = 0;
                        w->textinput.cursor = len;
                    }
                    win->dirty = 1;
                    return;
                }

                if (ch == '\b' || ch == 127) {
                    /* Backspace */
                    if (w->textinput.cursor > 0) {
                        memmove(w->textinput.text + w->textinput.cursor - 1,
                                w->textinput.text + w->textinput.cursor,
                                (size_t)(len - w->textinput.cursor + 1));
                        w->textinput.cursor--;
                    }
                } else if (ch == '\n' || ch == '\r') {
                    if (w->textinput.on_submit)
                        w->textinput.on_submit(win, fi);
                } else if (ch >= 32 && ch < 127) {
                    if (len < w->textinput.max_len) {
                        memmove(w->textinput.text + w->textinput.cursor + 1,
                                w->textinput.text + w->textinput.cursor,
                                (size_t)(len - w->textinput.cursor + 1));
                        w->textinput.text[w->textinput.cursor++] = ch;
                    }
                }
                win->dirty = 1;
            } else if (w->type == UI_BUTTON) {
                if (ev->key.key == '\n' || ev->key.key == ' ') {
                    if (w->button.on_click)
                        w->button.on_click(win, fi);
                    win->dirty = 1;
                }
            } else if (w->type == UI_CUSTOM) {
                if (w->custom.event)
                    w->custom.event(win, fi, ev);
                win->dirty = 1;
            }
        }
    }
}

/* ── Generic tick helper ───────────────────────────────────────── */

int uw_tick(ui_window_t *win, int mx, int my, int btn_down, int btn_up,
            int key)
{
    if (!win || win->wm_id < 0) return 0;

    /* Handle close */
    if (ui_window_close_requested(win->wm_id)) {
        ui_window_close_clear(win->wm_id);
        uw_destroy(win);
        return 0;
    }

    /* Check resize */
    ui_window_check_resize(win);

    ui_win_info_t info = ui_window_info(win->wm_id);
    int lx = mx - info.cx;
    int ly = my - info.cy;
    int inside = (lx >= 0 && ly >= 0 && lx < info.cw && ly < info.ch);

    /* Mouse down */
    if (btn_down && inside) {
        ui_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = UI_EVENT_MOUSE_DOWN;
        ev.mouse.x = mx;
        ev.mouse.y = my;
        ev.mouse.wx = lx;
        ev.mouse.wy = ly;
        ui_dispatch_event(win, &ev);
        ui_window_focus(win->wm_id);
        ui_window_raise(win->wm_id);
    }

    /* Mouse up */
    if (btn_up) {
        ui_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = UI_EVENT_MOUSE_UP;
        ev.mouse.x = mx;
        ev.mouse.y = my;
        ev.mouse.wx = lx;
        ev.mouse.wy = ly;
        ui_dispatch_event(win, &ev);
    }

    /* Mouse move (always send for hover tracking) */
    {
        static int prev_mx, prev_my;
        if (mx != prev_mx || my != prev_my) {
            ui_event_t ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = UI_EVENT_MOUSE_MOVE;
            ev.mouse.x = mx;
            ev.mouse.y = my;
            ev.mouse.wx = lx;
            ev.mouse.wy = ly;
            ui_dispatch_event(win, &ev);
            prev_mx = mx;
            prev_my = my;
        }
    }

    /* Key */
    if (key > 0) {
        ui_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = UI_EVENT_KEY_PRESS;
        ev.key.key = (char)key;
        extern int keyboard_get_ctrl(void);
        extern int keyboard_get_shift(void);
        ev.key.ctrl  = (uint8_t)keyboard_get_ctrl();
        ev.key.shift = (uint8_t)keyboard_get_shift();
        ui_dispatch_event(win, &ev);
    }

    /* Redraw if dirty */
    if (win->dirty)
        uw_redraw(win);

    return (btn_down && inside) ? 1 : 0;
}

/* ── Route a key to whichever pool window is focused ───────── */

int uw_route_key(int focused_wm_id, int key)
{
    int i;
    for (i = 0; i < UW_MAX_WINDOWS; i++) {
        if (uw_used[i] && uw_pool[i].wm_id == focused_wm_id) {
            ui_event_t ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = UI_EVENT_KEY_PRESS;
            ev.key.key = (char)key;
            extern int keyboard_get_ctrl(void);
            extern int keyboard_get_shift(void);
            ev.key.ctrl  = (uint8_t)keyboard_get_ctrl();
            ev.key.shift = (uint8_t)keyboard_get_shift();
            ui_dispatch_event(&uw_pool[i], &ev);
            if (uw_pool[i].dirty)
                uw_redraw(&uw_pool[i]);
            return 1;
        }
    }
    return 0;
}

/* ── Standard app loop (blocking) ──────────────────────────────── */

int ui_app_run(ui_window_t *win,
               void (*on_event)(ui_window_t *win, ui_event_t *ev))
{
    (void)win; (void)on_event;
    /* Not used — apps run via tick() from ui_shell's main loop */
    return 0;
}
