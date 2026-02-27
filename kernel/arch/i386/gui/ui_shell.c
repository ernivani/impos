/* ui_shell.c — UIKit desktop shell (Phase 4)
 *
 * Structurally identical to desktop.c but uses ui_window_* in place
 * of wm2_*.  Ready to be swapped in during Phase 5.
 *
 * Overlay priority (highest → lowest):
 *   Radial > Drawer > ContextMenu > Menubar > Settings > Windows
 */

#include <kernel/ui_shell.h>
#include <kernel/ui_window.h>
#include <kernel/compositor.h>
#include <kernel/gfx.h>
#include <kernel/ui_token.h>
#include <kernel/ui_font.h>
#include <kernel/menubar.h>
#include <kernel/wallpaper.h>
#include <kernel/radial.h>
#include <kernel/drawer.h>
#include <kernel/context_menu.h>
#include <kernel/app.h>
#include <kernel/anim.h>
#include <kernel/settings_app.h>
#include <kernel/terminal_app.h>
#include <kernel/filemgr.h>
#include <kernel/taskmgr.h>
#include <kernel/monitor_app.h>
#include <kernel/idt.h>
#include <kernel/mouse.h>
#include <string.h>
#include <stdio.h>

/* keyboard_getchar_nb lives in libc — not exposed in a public header */
extern int keyboard_getchar_nb(void);

/* ── Login re-init flag ─────────────────────────────────────────── */
static int ui_shell_first_run = 1;

void ui_shell_notify_login(void) { ui_shell_first_run = 1; }

/* ── Wallpaper ──────────────────────────────────────────────────── */

static comp_surface_t *wp_surf       = NULL;
static uint32_t        wp_last_t     = 0;

static void wallpaper_update(uint32_t now)
{
    if (!wp_surf) return;
    uint32_t throttle = wallpaper_is_transitioning() ? 2 : 8;
    if (now - wp_last_t < throttle) return;
    wp_last_t = now;
    wallpaper_draw(wp_surf->pixels, wp_surf->w, wp_surf->h, now);
    comp_surface_damage_all(wp_surf);
}

/* ── Demo / hint window ─────────────────────────────────────────── */

static int demo_id = -1;

static void u32str(uint32_t n, char *buf)
{
    char tmp[12]; int i = 0, j;
    if (!n) { tmp[i++] = '0'; }
    else    { while (n) { tmp[i++] = '0' + n % 10; n /= 10; } }
    for (j = 0; j < i / 2; j++) {
        char t = tmp[j]; tmp[j] = tmp[i-1-j]; tmp[i-1-j] = t;
    }
    tmp[i] = '\0';
    memcpy(buf, tmp, (size_t)i + 1);
}

static void demo_paint(void)
{
    if (demo_id < 0) return;
    int cw, ch;
    uint32_t *pix = ui_window_canvas(demo_id, &cw, &ch);
    if (!pix) return;

    gfx_surface_t gs = { pix, cw, ch, cw };

    gfx_surf_fill_rect(&gs, 0, 0, cw, ch, TOK_BG_SURFACE);
    gfx_surf_draw_string_smooth(&gs,  8,  8, "ImposOS Desktop",
                                TOK_TEXT_PRIMARY, 1);
    gfx_surf_draw_string_smooth(&gs,  8, 24, "UIKit Phase 4 Shell",
                                TOK_TEXT_SECONDARY, 1);
    gfx_surf_fill_rect(&gs, 8, 42, cw - 16, 1,
                       GFX_RGB(48, 54, 72));

    const char *hints[] = {
        "Space     - radial launcher",
        "Tab       - app drawer",
        "Esc       - close overlay",
        "Right-click desktop - menu",
        "Click menubar logo  - radial",
    };
    for (int i = 0; i < 5; i++)
        gfx_surf_draw_string_smooth(&gs, 8, 52 + i * 16,
                                    hints[i], TOK_TEXT_DIM, 1);

    /* FPS */
    char fps_str[32];
    u32str(compositor_get_fps(), fps_str);
    char line[48];
    snprintf(line, sizeof(line), "FPS: %s", fps_str);
    gfx_surf_draw_string_smooth(&gs, 8, 52 + 5 * 16,
                                line, TOK_ACCENT, 1);

    ui_window_damage_all(demo_id);
}

/* ── Init ───────────────────────────────────────────────────────── */

void ui_shell_init(void)
{
    int sw = (int)gfx_width();
    int sh = (int)gfx_height();

    compositor_init();
    ui_window_init();
    ui_font_init();
    anim_init();
    wallpaper_init();
    app_init();

    gfx_set_compositor_mode(1);

    /* Wallpaper surface */
    wp_surf = comp_surface_create(sw, sh, COMP_LAYER_WALLPAPER);
    if (wp_surf) {
        uint32_t t0 = pit_get_ticks();
        wallpaper_draw(wp_surf->pixels, sw, sh, t0);
        comp_surface_damage_all(wp_surf);
        wp_last_t = t0;
    }

    /* Cursor */
    comp_cursor_init();
    comp_cursor_move(mouse_get_x(), mouse_get_y());

    /* Overlays */
    menubar_init();
    radial_init();
    drawer_init();
    ctx_menu_init();

    /* Demo hint window */
    demo_id = ui_window_create(sw / 2 - 200, sh / 2 - 120,
                               400, 270, "ImposOS");
    demo_paint();
}

/* ── Main event loop ────────────────────────────────────────────── */

int ui_shell_run(void)
{
    static uint8_t prev_btn    = 0;
    static int     last_right  = 0;
    if (ui_shell_first_run) {
        ui_shell_first_run = 0;
        ui_shell_init();
    }

    while (1) {
        uint32_t now = pit_get_ticks();

        /* ── Wallpaper ───────────────────────────────────────────── */
        wallpaper_update(now);

        /* ── Mouse input ─────────────────────────────────────────── */
        if (mouse_poll()) {
            int     mx       = mouse_get_x();
            int     my       = mouse_get_y();
            uint8_t cur_btn  = mouse_get_buttons();
            int btn_down     = (cur_btn & 1) && !(prev_btn & 1);
            int btn_up       = !(cur_btn & 1) && (prev_btn & 1);
            int rbtn_down    = (cur_btn & 2) && !(prev_btn & 2);
            int rbtn_up      = !(cur_btn & 2) && (prev_btn & 2);

            if (rbtn_down) last_right = 1;
            int right_up = rbtn_up && last_right;
            if (rbtn_up)  last_right = 0;

            int consumed = 0;

            /* Priority 1: radial launcher */
            if (!consumed && radial_visible())
                consumed = radial_mouse(mx, my, btn_down, btn_up, 0);

            /* Priority 2: app drawer */
            if (!consumed && drawer_visible())
                consumed = drawer_mouse(mx, my, btn_down, btn_up,
                                        right_up);

            /* Priority 3: context menu */
            if (!consumed && ctx_menu_visible())
                consumed = ctx_menu_mouse(mx, my, btn_down, btn_up);

            /* Right-click on bare desktop → open context menu */
            if (!consumed && right_up &&
                !radial_visible() && !drawer_visible() &&
                !ctx_menu_visible() && my > MENUBAR_HEIGHT) {

                int on_win = 0;
                for (int id = 0; id < 32 && !on_win; id++) {
                    ui_win_info_t wi = ui_window_info(id);
                    if (wi.w <= 0 || wi.state == UI_WIN_MINIMIZED) continue;
                    if (mx >= wi.x && mx < wi.x + wi.w &&
                        my >= wi.y && my < wi.y + wi.h)
                        on_win = 1;
                }
                if (!on_win) { ctx_menu_show(mx, my); consumed = 1; }
            }

            /* Priority 4: menubar */
            if (!consumed)
                consumed = menubar_mouse(mx, my, btn_down, btn_up,
                                         right_up);

            /* Priority 5: settings window */
            if (!consumed && settings_win_open())
                consumed = settings_tick(mx, my, btn_down, btn_up);

            /* Priority 5b: terminal window */
            if (!consumed && terminal_app_win_open())
                consumed = terminal_app_tick(mx, my, btn_down, btn_up);

            /* Priority 5c: files window */
            if (!consumed && filemgr_win_open())
                consumed = filemgr_tick(mx, my, btn_down, btn_up);

            /* Priority 5d: task manager window */
            if (!consumed && taskmgr_win_open())
                consumed = taskmgr_tick(mx, my, btn_down, btn_up);

            /* Priority 5e: system monitor window */
            if (!consumed && monitor_win_open())
                consumed = monitor_tick(mx, my, btn_down, btn_up);

            /* Priority 6: window manager */
            if (!consumed)
                ui_window_mouse_event(mx, my, cur_btn, prev_btn);

            prev_btn = cur_btn;
            comp_cursor_move(mx, my);
        }

        /* ── Keyboard input ──────────────────────────────────────── */
        {
            int c = keyboard_getchar_nb();
            if (c > 0) {
                char ch = (char)c;
                int term_focused = terminal_app_win_open() &&
                    ui_window_focused() == terminal_app_win_id();

                if (radial_visible()) {
                    radial_key(ch, 0);
                } else if (drawer_visible()) {
                    drawer_key(ch, 0);
                } else if (term_focused) {
                    /* Route to terminal shell */
                    terminal_app_handle_key(ch);
                } else {
                    if (ch == ' ') {
                        if (ctx_menu_visible()) ctx_menu_hide();
                        radial_show();
                    } else if (ch == '\t') {
                        if (ctx_menu_visible()) ctx_menu_hide();
                        if (drawer_visible()) drawer_hide();
                        else                  drawer_show(0);
                    } else if (ch == 27) {
                        if      (ctx_menu_visible()) ctx_menu_hide();
                        else if (drawer_visible())   drawer_hide();
                        else if (radial_visible())   radial_hide();
                        else return DESKTOP_ACTION_POWER;
                    }
                }
            }
        }

        /* ── App ticks (close handling, auto-refresh) ────────── */
        if (terminal_app_win_open())
            terminal_app_tick(mouse_get_x(), mouse_get_y(), 0, 0);
        if (filemgr_win_open())
            filemgr_tick(mouse_get_x(), mouse_get_y(), 0, 0);
        if (taskmgr_win_open())
            taskmgr_tick(mouse_get_x(), mouse_get_y(), 0, 0);
        if (monitor_win_open())
            monitor_tick(mouse_get_x(), mouse_get_y(), 0, 0);

        /* ── Demo window lifecycle ───────────────────────────────── */
        if (demo_id >= 0 && ui_window_close_requested(demo_id)) {
            ui_window_close_clear(demo_id);
            ui_window_close_animated(demo_id);
            demo_id = -1;
            menubar_update_windows();
        }

        /* ── Repaint menubar once per second (clock/FPS text) ───────── */
        {
            static uint32_t last_mb = 0;
            if (now - last_mb >= 120) {
                last_mb = now;
                menubar_paint();
            }
        }

        /* ── Repaint demo window at ~8 Hz so content stays fresh ──── */
        {
            static uint32_t last_demo = 0;
            if (demo_id >= 0 && now - last_demo >= 15) {
                last_demo = now;
                demo_paint();
            }
        }

        /* ── Hide windows when full-screen overlays are open ─────── */
        {
            static int wins_hidden = 0;
            int should_hide = drawer_visible() || radial_visible();
            if (should_hide && !wins_hidden) {
                ui_window_set_all_visible(0);
                wins_hidden = 1;
            } else if (!should_hide && wins_hidden) {
                ui_window_set_all_visible(1);
                wins_hidden = 0;
            }
        }

        /* ── Animation tick ──────────────────────────────────────── */
        {
            static uint32_t last_anim = 0;
            if (now != last_anim) {
                uint32_t dt = (now - last_anim) * 8;
                anim_tick(dt);
                ui_window_tick();
                radial_tick();
                drawer_tick();
                ctx_menu_tick();
                last_anim = now;
            }
        }

        /* ── Composite frame ─────────────────────────────────────── */
        compositor_frame();

        /* Sleep until next PIT or device interrupt */
        asm volatile ("hlt");
    }
}
