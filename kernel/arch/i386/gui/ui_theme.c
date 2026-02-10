#include <kernel/ui_theme.h>

ui_theme_t ui_theme;

void ui_theme_init(void) {
    /* Desktop */
    ui_theme.desktop_bg        = 0x000000;
    ui_theme.surface           = GFX_RGB(20, 20, 20);
    ui_theme.border            = GFX_RGB(30, 30, 30);

    /* Text */
    ui_theme.text_primary      = GFX_RGB(230, 230, 230);
    ui_theme.text_dim          = GFX_RGB(51, 51, 51);
    ui_theme.text_secondary    = GFX_RGB(178, 178, 178);
    ui_theme.text_sub          = GFX_RGB(100, 100, 100);
    ui_theme.text_error        = GFX_RGB(180, 60, 60);

    /* Windows */
    ui_theme.win_bg            = 0x000000;
    ui_theme.win_header_bg     = GFX_RGB(48, 48, 48);
    ui_theme.win_header_focused= GFX_RGB(60, 60, 60);
    ui_theme.win_header_text   = GFX_RGB(220, 220, 220);
    ui_theme.win_body_bg       = GFX_RGB(30, 30, 30);
    ui_theme.win_border        = GFX_RGB(20, 20, 20);
    ui_theme.win_close_normal  = GFX_RGB(180, 60, 60);
    ui_theme.win_close_hover   = GFX_RGB(232, 17, 35);

    /* Taskbar / dock */
    ui_theme.taskbar_bg        = GFX_RGB(15, 15, 15);
    ui_theme.dock_pill_bg      = GFX_RGB(8, 8, 8);

    /* Selection / list */
    ui_theme.list_sel_bg       = GFX_RGB(30, 30, 30);

    /* Input fields */
    ui_theme.input_bg          = GFX_RGB(20, 20, 20);
    ui_theme.input_border      = GFX_RGB(30, 30, 30);
    ui_theme.input_border_focus= GFX_RGB(76, 76, 76);
    ui_theme.input_placeholder = GFX_RGB(50, 50, 50);

    /* Buttons */
    ui_theme.btn_bg            = GFX_RGB(45, 45, 45);
    ui_theme.btn_hover         = GFX_RGB(55, 55, 55);
    ui_theme.btn_pressed       = GFX_RGB(35, 35, 35);
    ui_theme.btn_text          = GFX_RGB(220, 220, 220);
    ui_theme.btn_primary_bg    = GFX_RGB(50, 100, 200);
    ui_theme.btn_primary_hover = GFX_RGB(65, 115, 215);
    ui_theme.btn_primary_text  = GFX_RGB(255, 255, 255);

    /* Checkbox */
    ui_theme.checkbox_bg       = GFX_RGB(20, 20, 20);
    ui_theme.checkbox_border   = GFX_RGB(60, 60, 60);
    ui_theme.checkbox_checked  = GFX_RGB(50, 100, 200);
    ui_theme.checkbox_check    = GFX_RGB(255, 255, 255);

    /* Tabs */
    ui_theme.tab_bg            = GFX_RGB(20, 20, 20);
    ui_theme.tab_active_bg     = GFX_RGB(40, 40, 50);
    ui_theme.tab_text          = GFX_RGB(120, 120, 120);
    ui_theme.tab_active_text   = GFX_RGB(80, 160, 240);

    /* Progress bar */
    ui_theme.progress_bg       = GFX_RGB(40, 40, 40);
    ui_theme.progress_fill     = GFX_RGB(80, 160, 240);
    ui_theme.progress_warn     = GFX_RGB(220, 60, 60);

    /* Accent / misc */
    ui_theme.accent            = GFX_RGB(80, 160, 240);
    ui_theme.dot               = GFX_RGB(128, 128, 128);
    ui_theme.icon              = GFX_RGB(190, 190, 190);
    ui_theme.icon_hi           = GFX_RGB(255, 255, 255);
    ui_theme.icon_dim          = GFX_RGB(90, 90, 90);

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
