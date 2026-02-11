#include <kernel/ui_theme.h>

ui_theme_t ui_theme;

void ui_theme_init(void) {
    /* Desktop — macOS Sonoma dark */
    ui_theme.desktop_bg        = 0x1E1E2E;
    ui_theme.surface           = GFX_RGB(30, 30, 46);
    ui_theme.border            = GFX_RGB(49, 50, 68);

    /* Text — Catppuccin-inspired on dark blue-gray */
    ui_theme.text_primary      = GFX_RGB(205, 214, 244);
    ui_theme.text_dim          = GFX_RGB(69, 71, 90);
    ui_theme.text_secondary    = GFX_RGB(166, 173, 200);
    ui_theme.text_sub          = GFX_RGB(108, 112, 134);
    ui_theme.text_error        = GFX_RGB(243, 139, 168);

    /* Windows */
    ui_theme.win_bg            = GFX_RGB(24, 24, 37);
    ui_theme.win_header_bg     = GFX_RGB(30, 30, 46);
    ui_theme.win_header_focused= GFX_RGB(49, 50, 68);
    ui_theme.win_header_text   = GFX_RGB(205, 214, 244);
    ui_theme.win_body_bg       = GFX_RGB(24, 24, 37);
    ui_theme.win_border        = GFX_RGB(49, 50, 68);
    ui_theme.win_close_normal  = GFX_RGB(243, 139, 168);
    ui_theme.win_close_hover   = GFX_RGB(245, 160, 185);

    /* Taskbar / dock */
    ui_theme.taskbar_bg        = GFX_RGB(17, 17, 27);
    ui_theme.dock_pill_bg      = GFX_RGB(24, 24, 37);

    /* Selection / list */
    ui_theme.list_sel_bg       = GFX_RGB(49, 50, 68);

    /* Input fields */
    ui_theme.input_bg          = GFX_RGB(30, 30, 46);
    ui_theme.input_border      = GFX_RGB(49, 50, 68);
    ui_theme.input_border_focus= GFX_RGB(137, 180, 250);
    ui_theme.input_placeholder = GFX_RGB(69, 71, 90);

    /* Buttons */
    ui_theme.btn_bg            = GFX_RGB(49, 50, 68);
    ui_theme.btn_hover         = GFX_RGB(59, 60, 78);
    ui_theme.btn_pressed       = GFX_RGB(39, 40, 58);
    ui_theme.btn_text          = GFX_RGB(205, 214, 244);
    ui_theme.btn_primary_bg    = GFX_RGB(137, 180, 250);
    ui_theme.btn_primary_hover = GFX_RGB(157, 195, 252);
    ui_theme.btn_primary_text  = GFX_RGB(17, 17, 27);

    /* Checkbox */
    ui_theme.checkbox_bg       = GFX_RGB(30, 30, 46);
    ui_theme.checkbox_border   = GFX_RGB(69, 71, 90);
    ui_theme.checkbox_checked  = GFX_RGB(137, 180, 250);
    ui_theme.checkbox_check    = GFX_RGB(17, 17, 27);

    /* Tabs */
    ui_theme.tab_bg            = GFX_RGB(24, 24, 37);
    ui_theme.tab_active_bg     = GFX_RGB(49, 50, 68);
    ui_theme.tab_text          = GFX_RGB(108, 112, 134);
    ui_theme.tab_active_text   = GFX_RGB(137, 180, 250);

    /* Progress bar */
    ui_theme.progress_bg       = GFX_RGB(49, 50, 68);
    ui_theme.progress_fill     = GFX_RGB(137, 180, 250);
    ui_theme.progress_warn     = GFX_RGB(243, 139, 168);

    /* Toggle */
    ui_theme.toggle_on_bg      = GFX_RGB(137, 180, 250);
    ui_theme.toggle_off_bg     = GFX_RGB(69, 71, 90);
    ui_theme.toggle_handle     = GFX_RGB(240, 240, 250);

    /* Card */
    ui_theme.card_bg           = GFX_RGB(30, 30, 46);
    ui_theme.card_border       = GFX_RGB(49, 50, 68);

    /* Semantic */
    ui_theme.danger            = GFX_RGB(243, 139, 168);
    ui_theme.success           = GFX_RGB(166, 227, 161);
    ui_theme.icon_grid_sel     = GFX_RGB(137, 180, 250);

    /* Accent / misc */
    ui_theme.accent            = GFX_RGB(137, 180, 250);
    ui_theme.dot               = GFX_RGB(108, 112, 134);
    ui_theme.icon              = GFX_RGB(166, 173, 200);
    ui_theme.icon_hi           = GFX_RGB(205, 214, 244);
    ui_theme.icon_dim          = GFX_RGB(69, 71, 90);

    /* Layout sizes */
    ui_theme.padding           = 8;
    ui_theme.spacing           = 6;
    ui_theme.border_radius     = 4;
    ui_theme.input_height      = 28;
    ui_theme.button_height     = 28;
    ui_theme.row_height        = 20;
    ui_theme.tab_height        = 28;
    ui_theme.taskbar_height    = 48;
    ui_theme.titlebar_height   = 28;
    ui_theme.win_border_width  = 1;
    ui_theme.close_btn_radius  = 6;
}
