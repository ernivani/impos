/* desktop.c — Phase 1 stub
 * Old monolithic desktop/dock/toast/alttab code removed.
 * desktop_run() shows a placeholder until Phase 4 (desktop shell rewrite).
 */
#include <kernel/desktop.h>
#include <kernel/gfx.h>
#include <kernel/ui_theme.h>
#include <kernel/wm.h>
#include <kernel/mouse.h>
#include <kernel/idt.h>
#include <kernel/io.h>
#include <stdio.h>
#include <string.h>

static int desktop_first_show = 1;

void desktop_notify_login(void) { desktop_first_show = 1; }

/* ── Placeholder screen ─────────────────────────────────────────── */

static void draw_placeholder(void) {
    int w = gfx_width(), h = gfx_height();
    uint32_t *bb = gfx_backbuffer();
    uint32_t pitch4 = gfx_pitch() / 4;

    /* Fill background */
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            bb[y * pitch4 + x] = ui_theme.desktop_bg;

    /* Centered label */
    const char *line1 = "ImposOS";
    const char *line2 = "Desktop rewrite in progress";
    const char *line3 = "[ Phase 1: old stack removed ]";

    int cx = w / 2;
    int cy = h / 2 - 24;
    gfx_draw_string(cx - (int)strlen(line1) * 4, cy,      line1, ui_theme.text_primary,   ui_theme.desktop_bg);
    gfx_draw_string(cx - (int)strlen(line2) * 4, cy + 20, line2, ui_theme.text_secondary,  ui_theme.desktop_bg);
    gfx_draw_string(cx - (int)strlen(line3) * 4, cy + 40, line3, ui_theme.text_dim,        ui_theme.desktop_bg);

    gfx_flip();
}

/* ── Public API stubs ───────────────────────────────────────────── */

void desktop_init(void)          { }
void desktop_draw_dock(void)     { }
void desktop_draw_menubar(void)  { }
void desktop_draw_chrome(void)   { }
void desktop_open_terminal(void) { }
void desktop_close_terminal(void){ }
void desktop_request_refresh(void){ }

int  desktop_dock_y(void)        { return gfx_height() - ui_theme.taskbar_height; }
int  desktop_dock_h(void)        { return ui_theme.taskbar_height; }
int  desktop_dock_x(void)        { return 0; }
int  desktop_dock_w(void)        { return gfx_width(); }
int  desktop_dock_items(void)    { return 0; }
int  desktop_dock_sep_pos(void)  { return 0; }
int  desktop_dock_item_rect(int idx, int *ox, int *oy, int *ow, int *oh) {
    (void)idx; if (ox) *ox=0; if (oy) *oy=0; if (ow) *ow=0; if (oh) *oh=0; return 0;
}
int  desktop_dock_action(int idx){ (void)idx; return 0; }

void (*desktop_get_idle_terminal_cb(void))(void) { return 0; }

/* Toast stubs */
void toast_show(const char *app, const char *title, const char *msg, int type) {
    (void)app; (void)title; (void)msg; (void)type;
}
int toast_handle_mouse(int mx, int my, int dn, int held, int up) {
    (void)mx; (void)my; (void)dn; (void)held; (void)up; return 0;
}

/* Alt-tab stubs */
static int alttab_visible = 0;
void alttab_activate(void)    { alttab_visible = 0; }
void alttab_confirm(void)     { alttab_visible = 0; }
void alttab_cancel(void)      { alttab_visible = 0; }
int  alttab_is_visible(void)  { return alttab_visible; }

/* ── Main desktop loop ──────────────────────────────────────────── */

int desktop_run(void) {
    if (desktop_first_show) {
        desktop_first_show = 0;
        draw_placeholder();
    }

    while (1) {
        char c = getchar();
        if (c == 27) /* ESC → power action */
            return DESKTOP_ACTION_POWER;
        /* All other keys: redraw placeholder */
        draw_placeholder();
    }
}
