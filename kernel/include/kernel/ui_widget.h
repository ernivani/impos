#ifndef _KERNEL_UI_WIDGET_H
#define _KERNEL_UI_WIDGET_H

#include <stdint.h>
#include <kernel/ui_event.h>

/* Widget types */
#define UI_LABEL      0
#define UI_BUTTON     1
#define UI_TEXTINPUT  2
#define UI_LIST       3
#define UI_CHECKBOX   4
#define UI_PROGRESS   5
#define UI_TABS       6
#define UI_PANEL      7
#define UI_SEPARATOR  8
#define UI_CUSTOM     9
#define UI_TOGGLE     10
#define UI_ICON_GRID  11
#define UI_CARD       12

/* Widget flags */
#define UI_FLAG_FOCUSABLE  0x01
#define UI_FLAG_VISIBLE    0x02
#define UI_FLAG_DISABLED   0x04
#define UI_FLAG_CAPTURING  0x08
#define UI_FLAG_HOVER      0x10

#define UI_MAX_WIDGETS     48
#define UI_TEXT_MAX        128
#define UI_LIST_MAX_ITEMS   64

struct ui_window;

typedef void (*ui_callback_t)(struct ui_window *win, int widget_idx);
typedef void (*ui_custom_draw_t)(struct ui_window *win, int widget_idx, uint32_t *canvas, int cw, int ch);
typedef int  (*ui_custom_event_t)(struct ui_window *win, int widget_idx, ui_event_t *ev);

typedef struct {
    int type;
    int x, y, w, h;
    uint16_t flags;
    int16_t parent;     /* -1 = root */

    union {
        /* UI_LABEL */
        struct {
            char text[UI_TEXT_MAX];
            uint32_t color;     /* 0 = use theme text_primary */
        } label;

        /* UI_BUTTON */
        struct {
            char text[48];
            uint8_t pressed;
            uint8_t primary;    /* use primary/accent colors */
            ui_callback_t on_click;
        } button;

        /* UI_TEXTINPUT */
        struct {
            char text[UI_TEXT_MAX];
            char placeholder[48];
            int cursor;
            int scroll;
            int max_len;
            int sel_start;      /* selection anchor (-1 = no selection) */
            uint8_t password;
            ui_callback_t on_submit;
        } textinput;

        /* UI_LIST */
        struct {
            const char **items;
            int count;
            int selected;
            int scroll;
            ui_callback_t on_select;
            ui_callback_t on_activate;
        } list;

        /* UI_CHECKBOX */
        struct {
            char text[48];
            uint8_t checked;
            ui_callback_t on_change;
        } checkbox;

        /* UI_PROGRESS */
        struct {
            int value;          /* 0-100 */
            char label[48];
        } progress;

        /* UI_TABS */
        struct {
            const char **labels;
            int count;
            int active;
            ui_callback_t on_change;
        } tabs;

        /* UI_PANEL */
        struct {
            char title[48];
        } panel;

        /* UI_CUSTOM */
        struct {
            ui_custom_draw_t draw;
            ui_custom_event_t event;
            void *userdata;
        } custom;

        /* UI_TOGGLE */
        struct {
            char text[48];
            uint8_t on;
            ui_callback_t on_change;
        } toggle;

        /* UI_ICON_GRID */
        struct {
            int cols;
            int cell_w, cell_h;
            int count;
            int selected;
            int scroll;
            const char **labels;
            void (*draw_icon)(int idx, int x, int y, int sel);
            ui_callback_t on_activate;
        } icon_grid;

        /* UI_CARD */
        struct {
            char title[48];
            uint32_t bg_color;
            int radius;
        } card;
    };
} ui_widget_t;

typedef struct ui_window {
    int wm_id;
    ui_widget_t widgets[UI_MAX_WIDGETS];
    int widget_count;
    int focused_widget;     /* index, -1 = none */
    int dirty;
    void *app_data;
    int prev_cw, prev_ch;   /* for resize detection */
} ui_window_t;

/* Window lifecycle (legacy widget-window; ui_window.h owns ui_window_* names) */
ui_window_t *uw_create(int x, int y, int w, int h, const char *title);
void         uw_destroy(ui_window_t *win);

/* Add widgets (returns widget index or -1) */
int ui_add_label(ui_window_t *win, int x, int y, int w, int h,
                 const char *text, uint32_t color);
int ui_add_button(ui_window_t *win, int x, int y, int w, int h,
                  const char *text, ui_callback_t on_click);
int ui_add_textinput(ui_window_t *win, int x, int y, int w, int h,
                     const char *placeholder, int max_len, int is_password);
int ui_add_list(ui_window_t *win, int x, int y, int w, int h,
                const char **items, int count);
int ui_add_checkbox(ui_window_t *win, int x, int y, int w, int h,
                    const char *text, int checked);
int ui_add_progress(ui_window_t *win, int x, int y, int w, int h,
                    int value, const char *label);
int ui_add_tabs(ui_window_t *win, int x, int y, int w, int h,
                const char **labels, int count);
int ui_add_panel(ui_window_t *win, int x, int y, int w, int h,
                 const char *title);
int ui_add_separator(ui_window_t *win, int x, int y, int w);
int ui_add_custom(ui_window_t *win, int x, int y, int w, int h,
                  ui_custom_draw_t draw_fn, ui_custom_event_t event_fn,
                  void *userdata);
int ui_add_toggle(ui_window_t *win, int x, int y, int w, int h,
                  const char *text, int on);
int ui_add_icon_grid(ui_window_t *win, int x, int y, int w, int h,
                     int cols, int cell_w, int cell_h,
                     const char **labels, int count,
                     void (*draw_icon)(int idx, int x, int y, int sel));
int ui_add_card(ui_window_t *win, int x, int y, int w, int h,
                const char *title, uint32_t bg_color, int radius);

/* Widget access */
ui_widget_t *ui_get_widget(ui_window_t *win, int idx);

/* Widget visibility helpers */
void ui_widget_set_visible(ui_window_t *win, int idx, int visible);
void ui_widget_set_visible_range(ui_window_t *win, int from, int to, int visible);

/* Event dispatch */
void ui_dispatch_event(ui_window_t *win, ui_event_t *ev);
void ui_focus_next(ui_window_t *win);
void ui_focus_prev(ui_window_t *win);

/* Rendering */
void uw_redraw(ui_window_t *win);

/* Check for canvas resize and adapt widget layout */
void ui_window_check_resize(ui_window_t *win);

/* Generic per-frame tick: dispatches mouse/key events, redraws if dirty.
   Returns 1 if a mouse click was consumed inside the window. */
int uw_tick(ui_window_t *win, int mx, int my, int btn_down, int btn_up,
            int key);

/* Route a keypress to whichever pool window matches focused_wm_id.
   Returns 1 if key was consumed. */
int uw_route_key(int focused_wm_id, int key);

/* Standard app loop: returns dock action or 0 on close */
int ui_app_run(ui_window_t *win, void (*on_event)(ui_window_t *win, ui_event_t *ev));

#endif
