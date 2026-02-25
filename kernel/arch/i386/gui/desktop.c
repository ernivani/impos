/* desktop.c — Phase 4-12: Full desktop shell
 *
 * Layers (bottom to top):
 *   WALLPAPER  — procedural animated wallpaper (5 styles)
 *   WINDOWS    — WM2 managed windows
 *   OVERLAY    — menubar, dock, radial, drawer, context menu
 *   CURSOR     — compositor software cursor
 *
 * Keyboard shortcuts:
 *   Space  — toggle radial launcher
 *   Tab    — toggle app drawer
 *   Escape — close topmost overlay → then focused window
 *
 * Mouse routing (priority order):
 *   radial > drawer > context_menu > menubar > dock > wm2
 */
#include <kernel/desktop.h>
#include <kernel/compositor.h>
#include <kernel/gfx.h>
#include <kernel/ui_theme.h>
#include <kernel/wm2.h>
#include <kernel/wm.h>
#include <kernel/menubar.h>
#include <kernel/dock.h>
#include <kernel/wallpaper.h>
#include <kernel/radial.h>
#include <kernel/drawer.h>
#include <kernel/context_menu.h>
#include <kernel/app.h>
#include <kernel/anim.h>
#include <kernel/settings_app.h>
#include <kernel/idt.h>
#include <kernel/io.h>
#include <kernel/mouse.h>
#include <kernel/rtc.h>
#include <stdio.h>
#include <string.h>

static int desktop_first_show = 1;

void desktop_notify_login(void) { desktop_first_show = 1; }

/* ── Wallpaper ──────────────────────────────────────────────────── */

static comp_surface_t *wallpaper_surf = 0;
static uint32_t wallpaper_last_t = 0;

static void wallpaper_update(uint32_t now) {
    if (!wallpaper_surf) return;
    /* Redraw every 2 ticks (at 120Hz PIT = ~60fps cap) */
    if (now - wallpaper_last_t < 2) return;
    wallpaper_last_t = now;
    wallpaper_draw(wallpaper_surf->pixels,
                   wallpaper_surf->w,
                   wallpaper_surf->h,
                   now);
    comp_surface_damage_all(wallpaper_surf);
}

/* ── Demo window (startup hint) ─────────────────────────────────── */

static int demo_id = -1;

static void u32_to_str(uint32_t n, char *buf, int *len) {
    char tmp[12]; int i = 0, j;
    if (!n) { tmp[i++] = '0'; }
    else { while (n) { tmp[i++] = '0' + n % 10; n /= 10; } }
    for (j = 0; j < i/2; j++) {
        char t = tmp[j]; tmp[j] = tmp[i-1-j]; tmp[i-1-j] = t;
    }
    tmp[i] = '\0';
    for (j = 0; j <= i; j++) buf[j] = tmp[j];
    *len = i;
}

static void demo_paint(void) {
    if (demo_id < 0) return;
    int cw, ch;
    uint32_t *canvas = wm2_get_canvas(demo_id, &cw, &ch);
    if (!canvas) return;

    gfx_surface_t gs;
    gs.buf = canvas; gs.w = cw; gs.h = ch; gs.pitch = cw;
    uint32_t bg = 0xFF1E1E2E;

    gfx_surf_fill_rect(&gs, 0, 0, cw, ch, bg);
    gfx_surf_draw_string(&gs,  8,  8, "ImposOS Desktop",  0xFFCDD6F4, bg);
    gfx_surf_draw_string(&gs,  8, 24, "Phase 4-12: Full Shell", 0xFFA6ADC8, bg);
    gfx_surf_fill_rect(&gs, 8, 42, cw-16, 1, 0xFF45475A);
    gfx_surf_draw_string(&gs,  8, 52, "Space  — radial launcher",  0xFF6C7086, bg);
    gfx_surf_draw_string(&gs,  8, 68, "Tab    — app drawer",       0xFF6C7086, bg);
    gfx_surf_draw_string(&gs,  8, 84, "Escape — close overlay",    0xFF6C7086, bg);
    gfx_surf_draw_string(&gs,  8, 100,"Right-click desktop → menu",0xFF6C7086, bg);
    gfx_surf_draw_string(&gs,  8, 116,"Click menubar logo → radial",0xFF6C7086, bg);
    {
        char nb[12]; int nl = 0;
        u32_to_str(compositor_get_fps(), nb, &nl);
        char line[32]; int li = 0;
        const char *pre = "FPS: ";
        while (*pre) line[li++] = *pre++;
        int i = 0; while (nb[i]) line[li++] = nb[i++];
        line[li] = '\0';
        gfx_surf_draw_string(&gs, 8, 140, line, 0xFF89B4FA, bg);
    }
    wm2_damage_canvas_all(demo_id);
}

/* ── Init ───────────────────────────────────────────────────────── */

void desktop_init(void) {
    int sw, sh;

    compositor_init();
    wm2_init();
    anim_init();
    wallpaper_init();
    app_init();

    wallpaper_surf = 0;
    demo_id = -1;

    sw = (int)gfx_width();
    sh = (int)gfx_height();

    gfx_set_compositor_mode(1);

    /* Wallpaper surface */
    wallpaper_surf = comp_surface_create(sw, sh, COMP_LAYER_WALLPAPER);
    if (wallpaper_surf) {
        uint32_t t0 = pit_get_ticks();
        wallpaper_draw(wallpaper_surf->pixels, sw, sh, t0);
        comp_surface_damage_all(wallpaper_surf);
        wallpaper_last_t = t0;
    }

    /* Cursor */
    comp_cursor_init();
    comp_cursor_move(mouse_get_x(), mouse_get_y());

    /* Overlay elements */
    menubar_init();
    dock_init();
    radial_init();
    drawer_init();
    ctx_menu_init();

    /* Demo hint window */
    demo_id = wm2_create(sw/2 - 200, sh/2 - 120, 400, 270, "ImposOS");
    demo_paint();
}

/* ── Public stubs ───────────────────────────────────────────────── */

void desktop_draw_dock(void)       { }
void desktop_draw_menubar(void)    { }
void desktop_draw_chrome(void)     { }
void desktop_open_terminal(void)   { app_launch("terminal"); }
void desktop_close_terminal(void)  { }
void desktop_request_refresh(void) { demo_paint(); }

int  desktop_dock_y(void)     { return gfx_height() - ui_theme.taskbar_height; }
int  desktop_dock_h(void)     { return ui_theme.taskbar_height; }
int  desktop_dock_x(void)     { return 0; }
int  desktop_dock_w(void)     { return gfx_width(); }
int  desktop_dock_items(void) { return 0; }
int  desktop_dock_sep_pos(void) { return 0; }
int  desktop_dock_item_rect(int idx, int *ox, int *oy, int *ow, int *oh) {
    (void)idx;
    if (ox) *ox = 0; if (oy) *oy = 0;
    if (ow) *ow = 0; if (oh) *oh = 0;
    return 0;
}
int  desktop_dock_action(int idx) { (void)idx; return 0; }
void (*desktop_get_idle_terminal_cb(void))(void) { return 0; }

void toast_show(const char *a, const char *t, const char *m, int type) {
    (void)a; (void)t; (void)m; (void)type;
}
int toast_handle_mouse(int mx, int my, int dn, int held, int up) {
    (void)mx; (void)my; (void)dn; (void)held; (void)up; return 0;
}

static int alttab_visible = 0;
void alttab_activate(void)   { alttab_visible = 0; }
void alttab_confirm(void)    { alttab_visible = 0; }
void alttab_cancel(void)     { alttab_visible = 0; }
int  alttab_is_visible(void) { return alttab_visible; }

/* ── Main loop ──────────────────────────────────────────────────── */

int desktop_run(void) {
    static uint8_t  prev_btn  = 0;
    static int      last_right = 0;

    if (desktop_first_show) {
        desktop_first_show = 0;
        desktop_init();
    }

    while (1) {
        uint32_t now = pit_get_ticks();

        /* ── Animate wallpaper ───────────────────────────────────── */
        wallpaper_update(now);

        /* ── Input: mouse ───────────────────────────────────────── */
        if (mouse_poll()) {
            int mx          = mouse_get_x();
            int my          = mouse_get_y();
            uint8_t cur_btn = mouse_get_buttons();
            int btn_down    = (cur_btn & 1) && !(prev_btn & 1);
            int btn_up      = !(cur_btn & 1) && (prev_btn & 1);
            int rbtn_down   = (cur_btn & 2) && !(prev_btn & 2);
            int rbtn_up     = !(cur_btn & 2) && (prev_btn & 2);

            /* Right-click state tracking */
            if (rbtn_down) last_right = 1;
            int right_click_up = rbtn_up && last_right;
            if (rbtn_up) last_right = 0;

            /* Route in priority order */
            int consumed = 0;

            /* Radial (highest priority overlay) */
            if (!consumed && radial_visible())
                consumed = radial_mouse(mx, my, btn_down, btn_up, 0);

            /* Drawer */
            if (!consumed && drawer_visible())
                consumed = drawer_mouse(mx, my, btn_down, btn_up, right_click_up);

            /* Context menu */
            if (!consumed && ctx_menu_visible())
                consumed = ctx_menu_mouse(mx, my, btn_down, btn_up);

            /* Right-click on desktop (not consumed by overlays) */
            if (!consumed && right_click_up &&
                !radial_visible() && !drawer_visible() && !ctx_menu_visible()) {
                /* Check if click is on desktop (not on any window) */
                int on_desktop = (my > MENUBAR_HEIGHT);
                /* Quick check: if WM2 would consume it, don't open context menu */
                /* We open context menu, WM gets first pass later */
                if (on_desktop) {
                    ctx_menu_show(mx, my);
                    consumed = 1;
                }
            }

            /* Menubar */
            if (!consumed)
                consumed = menubar_mouse(mx, my, btn_down, btn_up, right_click_up);

            /* Dock */
            if (!consumed)
                consumed = dock_mouse(mx, my, btn_down, btn_up);

            /* Settings window mouse */
            if (!consumed && settings_win_open())
                settings_tick(mx, my, btn_up);

            /* WM2 */
            if (!consumed)
                wm2_mouse_event(mx, my, cur_btn, prev_btn);

            prev_btn = cur_btn;
            comp_cursor_move(mx, my);
        }

        /* ── Input: keyboard ────────────────────────────────────── */
        {
            int c = keyboard_getchar_nb();
            if (c > 0) {
                char ch = (char)c;

                /* Radial/drawer get first pass on keys */
                if (radial_visible()) {
                    radial_key(ch, 0);
                } else if (drawer_visible()) {
                    drawer_key(ch, 0);
                } else {
                    /* Global shortcuts */
                    if (ch == ' ') {
                        /* Space: toggle radial */
                        if (ctx_menu_visible()) ctx_menu_hide();
                        radial_show();
                    } else if (ch == 9) {
                        /* Tab: toggle drawer */
                        if (ctx_menu_visible()) ctx_menu_hide();
                        if (drawer_visible()) drawer_hide();
                        else drawer_show(0);
                    } else if (ch == 27) {
                        /* Escape: close topmost overlay, then return power */
                        if (ctx_menu_visible()) { ctx_menu_hide(); }
                        else if (drawer_visible()) { drawer_hide(); }
                        else if (radial_visible()) { radial_hide(); }
                        else { return DESKTOP_ACTION_POWER; }
                    }
                }
            }
        }

        /* ── Dock actions ───────────────────────────────────────── */
        {
            int action = dock_consume_action();
            if (action >= 0) {
                switch (action) {
                case 0: app_launch("terminal"); break;
                case 1: app_launch("files");    break;
                case 2: app_launch("settings"); break;
                case 3: app_launch("monitor");  break;
                }
            }
        }

        /* ── Demo window lifecycle ──────────────────────────────── */
        if (demo_id >= 0 && wm2_close_requested(demo_id)) {
            int sw = (int)gfx_width(), sh = (int)gfx_height();
            wm2_destroy(demo_id);
            /* Don't recreate — let user close it */
            demo_id = -1;
            (void)sw; (void)sh;
            menubar_update_windows();
        }

        /* ── Repaint menubar every second (clock + focus changes) ── */
        {
            static uint32_t last_mb_tick = 0;
            if (now - last_mb_tick >= 120) {
                last_mb_tick = now;
                menubar_paint();
                if (demo_id >= 0) demo_paint();
            }
        }

        /* ── Animation tick ─────────────────────────────────────── */
        {
            static uint32_t last_anim_tick = 0;
            if (now != last_anim_tick) {
                /* ~120Hz PIT: each tick ~8.3ms */
                uint32_t dt = (now - last_anim_tick) * 8;
                anim_tick(dt);
                last_anim_tick = now;
            }
        }

        /* ── Composite ──────────────────────────────────────────── */
        compositor_frame();
    }
}
