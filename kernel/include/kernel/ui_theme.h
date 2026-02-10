#ifndef _KERNEL_UI_THEME_H
#define _KERNEL_UI_THEME_H

#include <stdint.h>
#include <kernel/gfx.h>

typedef struct {
    /* Desktop */
    uint32_t desktop_bg;
    uint32_t surface;
    uint32_t border;

    /* Text */
    uint32_t text_primary;
    uint32_t text_dim;
    uint32_t text_secondary;
    uint32_t text_sub;
    uint32_t text_error;

    /* Windows */
    uint32_t win_bg;
    uint32_t win_header_bg;
    uint32_t win_header_focused;
    uint32_t win_header_text;
    uint32_t win_body_bg;
    uint32_t win_border;
    uint32_t win_close_normal;
    uint32_t win_close_hover;

    /* Taskbar / dock */
    uint32_t taskbar_bg;
    uint32_t dock_pill_bg;

    /* Selection / list */
    uint32_t list_sel_bg;

    /* Input fields */
    uint32_t input_bg;
    uint32_t input_border;
    uint32_t input_border_focus;
    uint32_t input_placeholder;

    /* Buttons */
    uint32_t btn_bg;
    uint32_t btn_hover;
    uint32_t btn_pressed;
    uint32_t btn_text;
    uint32_t btn_primary_bg;
    uint32_t btn_primary_hover;
    uint32_t btn_primary_text;

    /* Checkbox */
    uint32_t checkbox_bg;
    uint32_t checkbox_border;
    uint32_t checkbox_checked;
    uint32_t checkbox_check;

    /* Tabs */
    uint32_t tab_bg;
    uint32_t tab_active_bg;
    uint32_t tab_text;
    uint32_t tab_active_text;

    /* Progress bar */
    uint32_t progress_bg;
    uint32_t progress_fill;
    uint32_t progress_warn;

    /* Toggle */
    uint32_t toggle_on_bg;
    uint32_t toggle_off_bg;
    uint32_t toggle_handle;

    /* Card */
    uint32_t card_bg;
    uint32_t card_border;

    /* Semantic */
    uint32_t danger;
    uint32_t success;
    uint32_t icon_grid_sel;

    /* Accent */
    uint32_t accent;
    uint32_t dot;
    uint32_t icon;
    uint32_t icon_hi;
    uint32_t icon_dim;

    /* Layout sizes */
    int padding;
    int spacing;
    int border_radius;
    int input_height;
    int button_height;
    int row_height;
    int tab_height;
    int taskbar_height;
    int titlebar_height;
    int win_border_width;
    int close_btn_radius;
} ui_theme_t;

extern ui_theme_t ui_theme;

void ui_theme_init(void);

#endif
