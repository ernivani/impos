/* desktop.c — Phase 2: uses compositor for all rendering
 *
 * Wallpaper surface created on COMP_LAYER_WALLPAPER.
 * Future phases add window surfaces, dock, menubar on higher layers.
 */
#include <kernel/desktop.h>
#include <kernel/compositor.h>
#include <kernel/gfx.h>
#include <kernel/ui_theme.h>
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
    uint32_t tl = GFX_RGB(28,  28,  45);
    uint32_t tr = GFX_RGB(18,  18,  32);
    uint32_t bl = GFX_RGB(45,  28,  38);
    uint32_t br = GFX_RGB(32,  18,  28);

    uint32_t *px = wallpaper->pixels;
    for (int y = 0; y < h; y++) {
        int vy = y * 255 / (h > 1 ? h - 1 : 1);
        uint32_t la_r = ((( tl>>16)&0xFF)*(255-vy) + ((bl>>16)&0xFF)*vy) >> 8;
        uint32_t la_g = ((( tl>> 8)&0xFF)*(255-vy) + ((bl>> 8)&0xFF)*vy) >> 8;
        uint32_t la_b = (((tl    )&0xFF)*(255-vy) + ((bl    )&0xFF)*vy) >> 8;
        uint32_t ra_r = ((( tr>>16)&0xFF)*(255-vy) + ((br>>16)&0xFF)*vy) >> 8;
        uint32_t ra_g = ((( tr>> 8)&0xFF)*(255-vy) + ((br>> 8)&0xFF)*vy) >> 8;
        uint32_t ra_b = (((tr    )&0xFF)*(255-vy) + ((br    )&0xFF)*vy) >> 8;
        for (int x = 0; x < w; x++) {
            int hx = x * 255 / (w > 1 ? w - 1 : 1);
            uint32_t r = (la_r*(255-hx) + ra_r*hx) >> 8;
            uint32_t g = (la_g*(255-hx) + ra_g*hx) >> 8;
            uint32_t b = (la_b*(255-hx) + ra_b*hx) >> 8;
            px[y * w + x] = 0xFF000000 | (r<<16) | (g<<8) | b;
        }
    }
    comp_surface_damage_all(wallpaper);
}

/* Placeholder text drawn on top of wallpaper */
static comp_surface_t *placeholder = 0;

static void placeholder_draw(void) {
    if (!placeholder) return;
    int w = placeholder->w, h = placeholder->h;
    /* Clear to transparent */
    memset(placeholder->pixels, 0, (size_t)w * h * 4);

    gfx_surface_t gs = comp_surface_lock(placeholder);
    int cx = w / 2, cy = h / 2;

    gfx_surf_draw_string_scaled(&gs, cx - 56, cy - 32,
        "ImposOS", ui_theme.text_primary, 2);
    gfx_surf_draw_string(&gs, cx - 112, cy + 8,
        "Desktop rewrite in progress — Phase 2", ui_theme.text_secondary, 0);
    gfx_surf_draw_string(&gs, cx - 96, cy + 28,
        "Compositor: damage-tracked, 60fps cap", ui_theme.text_dim, 0);

    /* FPS counter */
    char fps_buf[32];
    uint32_t fps = compositor_get_fps();
    /* simple itoa */
    int fi = 0;
    if (fps == 0) { fps_buf[fi++] = '0'; }
    else { uint32_t tmp = fps; int start = fi;
           while (tmp) { fps_buf[fi++] = '0' + tmp%10; tmp /= 10; }
           for (int l=start,r=fi-1; l<r; l++,r--) { char t=fps_buf[l]; fps_buf[l]=fps_buf[r]; fps_buf[r]=t; } }
    fps_buf[fi] = 0;
    char line[48]; int li = 0;
    const char *pre = "FPS: ";
    while (*pre) line[li++] = *pre++;
    for (int i = 0; fps_buf[i]; i++) line[li++] = fps_buf[i];
    line[li] = 0;
    gfx_surf_draw_string(&gs, cx - 24, cy + 48, line, ui_theme.accent, 0);

    comp_surface_damage_all(placeholder);
}

/* ── Init ───────────────────────────────────────────────────────── */

void desktop_init(void) {
    compositor_init();

    int w = (int)gfx_width(), h = (int)gfx_height();

    /* Wallpaper surface — full screen, drawn once */
    if (wallpaper) comp_surface_destroy(wallpaper);
    wallpaper = comp_surface_create(w, h, COMP_LAYER_WALLPAPER);
    if (wallpaper) wallpaper_draw();

    /* Placeholder overlay — full screen, transparent bg */
    if (placeholder) comp_surface_destroy(placeholder);
    placeholder = comp_surface_create(w, h, COMP_LAYER_OVERLAY);
    if (placeholder) placeholder_draw();
}

/* ── Public stubs ───────────────────────────────────────────────── */

void desktop_draw_dock(void)     { }
void desktop_draw_menubar(void)  { }
void desktop_draw_chrome(void)   { }
void desktop_open_terminal(void) { }
void desktop_close_terminal(void){ }
void desktop_request_refresh(void){ if (placeholder) placeholder_draw(); }

int  desktop_dock_y(void)        { return gfx_height() - ui_theme.taskbar_height; }
int  desktop_dock_h(void)        { return ui_theme.taskbar_height; }
int  desktop_dock_x(void)        { return 0; }
int  desktop_dock_w(void)        { return gfx_width(); }
int  desktop_dock_items(void)    { return 0; }
int  desktop_dock_sep_pos(void)  { return 0; }
int  desktop_dock_item_rect(int idx, int *ox, int *oy, int *ow, int *oh) {
    (void)idx; if (ox)*ox=0; if (oy)*oy=0; if (ow)*ow=0; if (oh)*oh=0; return 0;
}
int  desktop_dock_action(int idx){ (void)idx; return 0; }
void (*desktop_get_idle_terminal_cb(void))(void) { return 0; }

void toast_show(const char *a, const char *t, const char *m, int type) {
    (void)a;(void)t;(void)m;(void)type;
}
int toast_handle_mouse(int mx, int my, int dn, int held, int up) {
    (void)mx;(void)my;(void)dn;(void)held;(void)up; return 0;
}

static int alttab_visible = 0;
void alttab_activate(void)   { alttab_visible = 0; }
void alttab_confirm(void)    { alttab_visible = 0; }
void alttab_cancel(void)     { alttab_visible = 0; }
int  alttab_is_visible(void) { return alttab_visible; }

/* ── Main loop ──────────────────────────────────────────────────── */

int desktop_run(void) {
    if (desktop_first_show) {
        desktop_first_show = 0;
        desktop_init();
    }

    /* Redraw FPS every ~60 frames */
    static uint32_t last_fps_update = 0;
    uint32_t tick = pit_get_ticks();

    while (1) {
        /* Refresh FPS display once per second */
        if (tick - last_fps_update >= 120) {
            last_fps_update = tick;
            if (placeholder) placeholder_draw();
        }

        compositor_frame();
        tick = pit_get_ticks();

        /* Non-blocking keyboard check */
        if (keyboard_data_available()) {
            char c = getchar();
            if (c == 27) return DESKTOP_ACTION_POWER; /* ESC */
        }

        /* Brief idle */
        __asm__ volatile("hlt");
        tick = pit_get_ticks();
    }
}
