/* desktop.c — Phase 3: compositor + window manager
 *
 * Wallpaper surface on COMP_LAYER_WALLPAPER (drawn once).
 * Demo window on COMP_LAYER_WINDOWS via wm2 (shows FPS + usage hints).
 * Mouse events routed to wm2 every frame; cursor drawn after composite.
 * Phase 4 will add the dock + menubar surfaces on top.
 */
#include <kernel/desktop.h>
#include <kernel/compositor.h>
#include <kernel/gfx.h>
#include <kernel/ui_theme.h>
#include <kernel/wm2.h>
#include <kernel/wm.h>
#include <kernel/idt.h>
#include <kernel/io.h>
#include <kernel/mouse.h>
#include <stdio.h>
#include <string.h>

static int desktop_first_show = 1;

void desktop_notify_login(void) { desktop_first_show = 1; }

/* ── Wallpaper ──────────────────────────────────────────────────── */

static comp_surface_t *wallpaper = 0;

static void wallpaper_draw(void) {
    if (!wallpaper) return;
    int w = wallpaper->w, h = wallpaper->h;

    /* Bilinear 4-corner gradient */
    uint32_t tl = GFX_RGB(28, 28, 45);
    uint32_t tr = GFX_RGB(18, 18, 32);
    uint32_t bl = GFX_RGB(45, 28, 38);
    uint32_t br = GFX_RGB(32, 18, 28);

    uint32_t *px = wallpaper->pixels;
    for (int y = 0; y < h; y++) {
        int vy = y * 255 / (h > 1 ? h-1 : 1);
        uint32_t la_r = (((tl>>16)&0xFF)*(255-vy) + ((bl>>16)&0xFF)*vy) >> 8;
        uint32_t la_g = (((tl>> 8)&0xFF)*(255-vy) + ((bl>> 8)&0xFF)*vy) >> 8;
        uint32_t la_b = (((tl    )&0xFF)*(255-vy) + ((bl    )&0xFF)*vy) >> 8;
        uint32_t ra_r = (((tr>>16)&0xFF)*(255-vy) + ((br>>16)&0xFF)*vy) >> 8;
        uint32_t ra_g = (((tr>> 8)&0xFF)*(255-vy) + ((br>> 8)&0xFF)*vy) >> 8;
        uint32_t ra_b = (((tr    )&0xFF)*(255-vy) + ((br    )&0xFF)*vy) >> 8;
        for (int x = 0; x < w; x++) {
            int hx = x * 255 / (w > 1 ? w-1 : 1);
            uint32_t r = (la_r*(255-hx) + ra_r*hx) >> 8;
            uint32_t g = (la_g*(255-hx) + ra_g*hx) >> 8;
            uint32_t b = (la_b*(255-hx) + ra_b*hx) >> 8;
            px[y*w + x] = 0xFF000000 | (r<<16) | (g<<8) | b;
        }
    }
    comp_surface_damage_all(wallpaper);
}

/* ── Demo window ────────────────────────────────────────────────── */

static int demo_id = -1;

/* Tiny uint→string helper */
static void u32_to_str(uint32_t n, char *buf, int *len) {
    char tmp[12];
    int i = 0, j;
    if (n == 0) { tmp[i++] = '0'; }
    else { while (n) { tmp[i++] = '0' + n%10; n /= 10; } }
    for (j = 0; j < i/2; j++) {
        char t = tmp[j]; tmp[j] = tmp[i-1-j]; tmp[i-1-j] = t;
    }
    tmp[i] = '\0';
    for (j = 0; j <= i; j++) buf[j] = tmp[j];
    *len = i;
}

static void demo_paint(void) {
    int cw, ch;
    uint32_t *canvas;
    gfx_surface_t gs;
    char line[64];
    int li;
    uint32_t bg = 0xFF1E1E2E;

    if (demo_id < 0) return;
    canvas = wm2_get_canvas(demo_id, &cw, &ch);
    if (!canvas) return;

    gs.buf = canvas;  gs.w = cw;  gs.h = ch;  gs.pitch = cw;

    /* Background */
    gfx_surf_fill_rect(&gs, 0, 0, cw, ch, bg);

    /* Header */
    gfx_surf_draw_string(&gs,  8,  8, "ImposOS Desktop",
                         0xFFCDD6F4, bg);
    gfx_surf_draw_string(&gs,  8, 24, "Phase 3: Window Manager",
                         0xFFA6ADC8, bg);

    /* Divider */
    gfx_surf_fill_rect(&gs, 8, 42, cw-16, 1, 0xFF45475A);

    /* Usage hints */
    gfx_surf_draw_string(&gs,  8, 52, "Drag title bar  — move window",
                         0xFF6C7086, bg);
    gfx_surf_draw_string(&gs,  8, 68, "Drag edges/corners — resize",
                         0xFF6C7086, bg);
    gfx_surf_draw_string(&gs,  8, 84, "Traffic lights  — close/min/max",
                         0xFF6C7086, bg);

    /* FPS */
    {
        char nb[12]; int nl = 0;
        u32_to_str(compositor_get_fps(), nb, &nl);
        li = 0;
        const char *pre = "FPS: ";
        while (*pre) line[li++] = *pre++;
        int i = 0; while (nb[i]) line[li++] = nb[i++];
        line[li] = '\0';
    }
    gfx_surf_draw_string(&gs, 8, 108, line, 0xFF89B4FA, bg);

    wm2_damage_canvas_all(demo_id);
}

/* ── Init ───────────────────────────────────────────────────────── */

void desktop_init(void) {
    int sw, sh;

    compositor_init();
    wm2_init();

    /* Old pointers are invalid after compositor_init() pool reset */
    wallpaper = 0;
    demo_id   = -1;

    sw = (int)gfx_width();
    sh = (int)gfx_height();

    /* Wallpaper — full screen, drawn once */
    wallpaper = comp_surface_create(sw, sh, COMP_LAYER_WALLPAPER);
    if (wallpaper) wallpaper_draw();

    /* Demo window — centred */
    demo_id = wm2_create(sw/2 - 200, sh/2 - 150, 400, 300, "ImposOS");
    demo_paint();
}

/* ── Public stubs ───────────────────────────────────────────────── */

void desktop_draw_dock(void)       { }
void desktop_draw_menubar(void)    { }
void desktop_draw_chrome(void)     { }
void desktop_open_terminal(void)   { }
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
    if (ox) *ox = 0;
    if (oy) *oy = 0;
    if (ow) *ow = 0;
    if (oh) *oh = 0;
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
    static uint8_t prev_btn = 0;

    if (desktop_first_show) {
        desktop_first_show = 0;
        desktop_init();
    }

    while (1) {
        /* Route mouse events to wm2 */
        if (mouse_poll()) {
            int mx          = mouse_get_x();
            int my          = mouse_get_y();
            uint8_t cur_btn = mouse_get_buttons();
            wm2_mouse_event(mx, my, cur_btn, prev_btn);
            prev_btn = cur_btn;
        }

        /* Non-blocking keyboard */
        if (keyboard_data_available()) {
            char c = getchar();
            if (c == 27) return DESKTOP_ACTION_POWER; /* ESC */
        }

        /* Handle close button on demo window: reopen it */
        if (demo_id >= 0 && wm2_close_requested(demo_id)) {
            int sw = (int)gfx_width(), sh = (int)gfx_height();
            wm2_destroy(demo_id);
            demo_id = wm2_create(sw/2 - 200, sh/2 - 150, 400, 300, "ImposOS");
        }

        /* Always repaint the demo window — keeps FPS live and ensures
           the compositor always has a dirty region to composite.
           compositor_frame() rate-limits the actual flip to 60fps. */
        demo_paint();

        /* Composite dirty regions (rate-capped at 60fps internally) */
        compositor_frame();

        /* Redraw cursor on framebuffer after composite flip */
        gfx_sync_cursor_after_composite(mouse_get_x(), mouse_get_y());

        /* Brief idle: wakes on PIT (120Hz), mouse (IRQ12), keyboard (IRQ1) */
        __asm__ volatile("hlt");
    }
}
