/* ui_widget.c — Phase 1 stub
 * Old widget system removed. Replaced by app_class_t model in Phase 3/6.
 */
#include <kernel/ui_widget.h>
#include <kernel/ui_event.h>
#include <kernel/wm.h>
#include <stdlib.h>
#include <string.h>

ui_window_t *uw_create(int x, int y, int w, int h, const char *title) {
    (void)x;(void)y;(void)w;(void)h;(void)title; return 0;
}
void uw_destroy(ui_window_t *win)  { (void)win; }
void uw_redraw(ui_window_t *win)   { (void)win; }
void ui_window_check_resize(ui_window_t *win) { (void)win; }

int ui_add_label(ui_window_t *win, int x, int y, int w, int h,
                 const char *text, uint32_t color)
    { (void)win;(void)x;(void)y;(void)w;(void)h;(void)text;(void)color; return -1; }

int ui_add_button(ui_window_t *win, int x, int y, int w, int h,
                  const char *text, ui_callback_t on_click)
    { (void)win;(void)x;(void)y;(void)w;(void)h;(void)text;(void)on_click; return -1; }

int ui_add_textinput(ui_window_t *win, int x, int y, int w, int h,
                     const char *placeholder, int max_len, int is_password)
    { (void)win;(void)x;(void)y;(void)w;(void)h;(void)placeholder;(void)max_len;(void)is_password; return -1; }

int ui_add_list(ui_window_t *win, int x, int y, int w, int h,
                const char **items, int count)
    { (void)win;(void)x;(void)y;(void)w;(void)h;(void)items;(void)count; return -1; }

int ui_add_checkbox(ui_window_t *win, int x, int y, int w, int h,
                    const char *text, int checked)
    { (void)win;(void)x;(void)y;(void)w;(void)h;(void)text;(void)checked; return -1; }

int ui_add_progress(ui_window_t *win, int x, int y, int w, int h,
                    int value, const char *label)
    { (void)win;(void)x;(void)y;(void)w;(void)h;(void)value;(void)label; return -1; }

int ui_add_tabs(ui_window_t *win, int x, int y, int w, int h,
                const char **labels, int count)
    { (void)win;(void)x;(void)y;(void)w;(void)h;(void)labels;(void)count; return -1; }

int ui_add_panel(ui_window_t *win, int x, int y, int w, int h, const char *title)
    { (void)win;(void)x;(void)y;(void)w;(void)h;(void)title; return -1; }

int ui_add_separator(ui_window_t *win, int x, int y, int w)
    { (void)win;(void)x;(void)y;(void)w; return -1; }

int ui_add_custom(ui_window_t *win, int x, int y, int w, int h,
                  ui_custom_draw_t draw_fn, ui_custom_event_t event_fn, void *userdata)
    { (void)win;(void)x;(void)y;(void)w;(void)h;(void)draw_fn;(void)event_fn;(void)userdata; return -1; }

int ui_add_toggle(ui_window_t *win, int x, int y, int w, int h,
                  const char *text, int on)
    { (void)win;(void)x;(void)y;(void)w;(void)h;(void)text;(void)on; return -1; }

int ui_add_icon_grid(ui_window_t *win, int x, int y, int w, int h,
                     int cols, int cell_w, int cell_h,
                     const char **labels, int count,
                     void (*draw_icon)(int idx, int x, int y, int sel))
    { (void)win;(void)x;(void)y;(void)w;(void)h;(void)cols;(void)cell_w;(void)cell_h;
      (void)labels;(void)count;(void)draw_icon; return -1; }

int ui_add_card(ui_window_t *win, int x, int y, int w, int h,
                const char *title, uint32_t bg_color, int radius)
    { (void)win;(void)x;(void)y;(void)w;(void)h;(void)title;(void)bg_color;(void)radius; return -1; }

ui_widget_t *ui_get_widget(ui_window_t *win, int idx) { (void)win;(void)idx; return 0; }

void ui_widget_set_visible(ui_window_t *win, int idx, int visible)
    { (void)win;(void)idx;(void)visible; }
void ui_widget_set_visible_range(ui_window_t *win, int from, int to, int visible)
    { (void)win;(void)from;(void)to;(void)visible; }

void ui_dispatch_event(ui_window_t *win, ui_event_t *ev) { (void)win;(void)ev; }
void ui_focus_next(ui_window_t *win) { (void)win; }
void ui_focus_prev(ui_window_t *win) { (void)win; }

int ui_app_run(ui_window_t *win, void (*on_event)(ui_window_t *win, ui_event_t *ev))
    { (void)win;(void)on_event; return 0; }
