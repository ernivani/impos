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

/* Widget flags */
#define UI_FLAG_FOCUSABLE  0x01
#define UI_FLAG_VISIBLE    0x02
#define UI_FLAG_DISABLED   0x04
#define UI_FLAG_CAPTURING  0x08
#define UI_FLAG_HOVER      0x10

#define UI_MAX_WIDGETS     32
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
    };
} ui_widget_t;

typedef struct ui_window {
    int wm_id;
    ui_widget_t widgets[UI_MAX_WIDGETS];
    int widget_count;
    int focused_widget;     /* index, -1 = none */
    int dirty;
    void *app_data;
} ui_window_t;

/* Window lifecycle */
ui_window_t *ui_window_create(int x, int y, int w, int h, const char *title);
void         ui_window_destroy(ui_window_t *win);

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

/* Widget access */
ui_widget_t *ui_get_widget(ui_window_t *win, int idx);

/* Event dispatch */
void ui_dispatch_event(ui_window_t *win, ui_event_t *ev);
void ui_focus_next(ui_window_t *win);
void ui_focus_prev(ui_window_t *win);

/* Rendering */
void ui_window_redraw(ui_window_t *win);

/* Standard app loop: returns dock action or 0 on close */
int ui_app_run(ui_window_t *win, void (*on_event)(ui_window_t *win, ui_event_t *ev));

#endif
