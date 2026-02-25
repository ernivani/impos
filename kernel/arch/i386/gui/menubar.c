/* menubar.c -- top bar: logo, focused app name, clock, system tray
 *
 * Single compositor surface on COMP_LAYER_OVERLAY, full screen width,
 * MENUBAR_HEIGHT pixels tall. Redrawn once per second (clock tick) or
 * on focus change.
 */
#include <kernel/menubar.h>
#include <kernel/compositor.h>
#include <kernel/gfx.h>
#include <kernel/ui_theme.h>
#include <kernel/wm2.h>
#include <kernel/rtc.h>
#include <kernel/config.h>
#include <kernel/idt.h>
#include <string.h>

static comp_surface_t *bar = 0;

/* ── Tiny helpers ─────────────────────────────────────────────────── */

static void u8_to_2dig(uint8_t v, char *out) {
    out[0] = '0' + v / 10;
    out[1] = '0' + v % 10;
}

/* ── Paint ────────────────────────────────────────────────────────── */

void menubar_paint(void) {
    if (!bar) return;

    int w = bar->w;
    gfx_surface_t gs;
    gs.buf = bar->pixels;  gs.w = w;  gs.h = MENUBAR_HEIGHT;  gs.pitch = w;

    /* Background - semi-transparent dark */
    uint32_t bg = 0xE011111B; /* alpha=224, Catppuccin crust */
    for (int i = 0; i < w * MENUBAR_HEIGHT; i++)
        bar->pixels[i] = bg;

    /* Subtle bottom border */
    for (int x = 0; x < w; x++)
        bar->pixels[(MENUBAR_HEIGHT - 1) * w + x] = 0xFF1E1E2E;

    /* Left: logo */
    gfx_surf_draw_string(&gs, 10, 4, "ImposOS", ui_theme.accent, bg);

    /* Center: focused window title */
    {
        int fid = wm2_get_focused();
        if (fid >= 0) {
            wm2_info_t info = wm2_get_info(fid);
            if (info.title[0]) {
                int len = 0;
                const char *p = info.title;
                while (*p++) len++;
                int tx = (w - len * 8) / 2;
                if (tx < 80) tx = 80;
                gfx_surf_draw_string(&gs, tx, 4, info.title,
                                     ui_theme.text_primary, bg);
            }
        }
    }

    /* Right: clock HH:MM */
    {
        datetime_t dt;
        rtc_read(&dt);
        char clock[6];
        u8_to_2dig(dt.hour, clock);
        clock[2] = ':';
        u8_to_2dig(dt.minute, clock + 3);
        clock[5] = '\0';
        gfx_surf_draw_string(&gs, w - 50, 4, clock,
                             ui_theme.text_primary, bg);
    }

    comp_surface_damage_all(bar);
}

/* ── Init ─────────────────────────────────────────────────────────── */

void menubar_init(void) {
    int sw = (int)gfx_width();
    bar = comp_surface_create(sw, MENUBAR_HEIGHT, COMP_LAYER_OVERLAY);
    if (!bar) return;
    bar->screen_x = 0;
    bar->screen_y = 0;
    menubar_paint();
}

/* ── Mouse ────────────────────────────────────────────────────────── */

int menubar_mouse(int mx, int my, int down) {
    (void)mx; (void)down;
    if (!bar) return 0;
    /* Consume clicks inside the menubar area */
    if (my < MENUBAR_HEIGHT) return 1;
    return 0;
}
