/* menubar.c — Phase 4+: top bar with logo, window pills, clock.
 *
 * Layout: [ ImposOS ] [ File  Edit  View ] [ window pills... ] ... [ Clock ]
 *
 * Logo click → radial launcher.
 * Window pills: one per open/minimized window.
 * Active pill: white text, blue underline.
 * Minimized pill: dim + italic (drawn as 35% opacity).
 * Clock: HH:MM, updated every second.
 */
#include <kernel/menubar.h>
#include <kernel/compositor.h>
#include <kernel/gfx.h>
#include <kernel/ui_theme.h>
#include <kernel/ui_window.h>
#include <kernel/rtc.h>
#include <kernel/idt.h>
#include <string.h>

#define MENUBAR_BG   0xB80C1016  /* rgba(12,16,22,0.72) — matches mockup */
#define PILL_ACTIVE_BG   0x1AFFFFFF
#define PILL_MIN_BG      0x08FFFFFF
#define PILL_HOVER_BG    0x26FFFFFF

static comp_surface_t *bar = 0;

/* Track pill positions for hit-testing */
#define MAX_PILLS 32
static struct {
    int win_id;
    int x, w;
} pills[MAX_PILLS];
static int pill_count = 0;

/* Logo click zone */
static int logo_x = 0, logo_w = 64;

/* ── Tiny helpers ─────────────────────────────────────────────────── */

static void u8_to_2dig(uint8_t v, char *out) {
    out[0] = '0' + v / 10;
    out[1] = '0' + v % 10;
}

static int strlenx(const char *s) {
    int n = 0; while (*s++) n++; return n;
}

/* Alpha-blend a pixel */
static void blend_px(uint32_t *p, uint32_t color, int alpha) {
    int sr = (color >> 16) & 0xFF;
    int sg = (color >>  8) & 0xFF;
    int sb =  color        & 0xFF;
    int dr = (*p >> 16) & 0xFF;
    int dg = (*p >>  8) & 0xFF;
    int db =  *p        & 0xFF;
    *p = 0xFF000000 |
         ((uint32_t)(dr + (sr - dr) * alpha / 255) << 16) |
         ((uint32_t)(dg + (sg - dg) * alpha / 255) <<  8) |
         (uint32_t)(db + (sb - db) * alpha / 255);
}

/* ── Paint ────────────────────────────────────────────────────────── */

void menubar_paint(void) {
    if (!bar) return;

    int w = bar->w;
    uint32_t *px = bar->pixels;
    gfx_surface_t gs;
    gs.buf = px; gs.w = w; gs.h = MENUBAR_HEIGHT; gs.pitch = w;

    /* Background: semi-transparent dark */
    for (int i = 0; i < w * MENUBAR_HEIGHT; i++) px[i] = MENUBAR_BG;

    /* Bottom border: alpha-blend 8% white over the bar background */
    for (int x = 0; x < w; x++)
        blend_px(&px[(MENUBAR_HEIGHT - 1) * w + x], 0xFFFFFFFF, 20);

    int cx = 0; /* current x cursor */

    /* ── Logo ───────────────────────────────────────────────────── */
    gfx_surf_draw_string_smooth(&gs, 10, (MENUBAR_HEIGHT - 16) / 2,
                               "ImposOS", ui_theme.accent, 1);
    logo_x = 4;
    logo_w = 7 * 8 + 12;
    cx = logo_x + logo_w;

    /* ── Menu labels ────────────────────────────────────────────── */
    {
        static const char *menus[] = { "File", "Edit", "View" };
        for (int m = 0; m < 3; m++) {
            gfx_surf_draw_string_smooth(&gs, cx, (MENUBAR_HEIGHT - 16) / 2,
                                       menus[m], 0x99CDD6F4, 1);
            cx += strlenx(menus[m]) * 8 + 16;
        }
        cx += 8;
    }

    /* ── Window pills ───────────────────────────────────────────── */
    pill_count = 0;
    int pills_start_x = cx;
    (void)pills_start_x;

    for (int wid = 0; wid < 32 && pill_count < MAX_PILLS; wid++) {
        ui_win_info_t info = ui_window_info(wid);
        if (info.w <= 0) continue;
        if (!info.title[0]) continue;

        int minimized = (info.state == UI_WIN_MINIMIZED);
        int focused   = info.focused;

        /* Pill width = text + padding */
        int tlen = strlenx(info.title);
        int pill_w = tlen * 8 + 20;
        if (pill_w > 160) pill_w = 160;

        int pill_x = cx;
        int pill_y = (MENUBAR_HEIGHT - 20) / 2;
        int pill_h = 20;

        /* Background fill */
        uint32_t pill_bg = focused  ? PILL_ACTIVE_BG :
                           minimized ? PILL_MIN_BG    : 0;
        if (pill_bg) {
            int alpha = (pill_bg >> 24) & 0xFF;
            int r = (pill_bg >> 16) & 0xFF;
            int g = (pill_bg >>  8) & 0xFF;
            int b =  pill_bg        & 0xFF;
            for (int row = pill_y; row < pill_y + pill_h; row++) {
                for (int col = pill_x; col < pill_x + pill_w; col++) {
                    if (col >= w) break;
                    blend_px(&px[row * w + col],
                             0xFF000000 | (r<<16) | (g<<8) | b, alpha);
                }
            }
        }

        /* Label: clip to pill_w - 8 */
        char label[64];
        int label_len = tlen;
        if (label_len > (pill_w - 8) / 8) label_len = (pill_w - 8) / 8;
        for (int i = 0; i < label_len; i++) label[i] = info.title[i];
        label[label_len] = '\0';

        uint32_t fg = focused   ? 0xD9FFFFFF :
                      minimized ? 0x59CDD6F4  :
                                  0xA6CDD6F4;
        gfx_surf_draw_string_smooth(&gs, pill_x + 10,
                                   (MENUBAR_HEIGHT - 16) / 2, label, fg, 1);

        /* Blue underline for active pill */
        if (focused && !minimized) {
            int ul_w = pill_w / 2;
            int ul_x = pill_x + (pill_w - ul_w) / 2;
            int ul_y = MENUBAR_HEIGHT - 3;
            for (int col = ul_x; col < ul_x + ul_w && col < w; col++)
                px[ul_y * w + col] = 0xFF3478F6;
        }

        /* Store pill for hit-testing */
        pills[pill_count].win_id = wid;
        pills[pill_count].x = pill_x;
        pills[pill_count].w = pill_w;
        pill_count++;

        cx += pill_w + 4;
    }

    /* ── Clock (right-aligned) ──────────────────────────────────── */
    {
        datetime_t dt;
        rtc_read(&dt);

        /* Weekday abbreviations */
        static const char *days[7] = { "Sun","Mon","Tue","Wed","Thu","Fri","Sat" };
        static const char *months[12] = {
            "Jan","Feb","Mar","Apr","May","Jun",
            "Jul","Aug","Sep","Oct","Nov","Dec"
        };

        /* Zeller's congruence: compute weekday from y/m/d
           0=Sun, 1=Mon, ..., 6=Sat */
        int zy = dt.year, zm = dt.month, zd = dt.day;
        if (zm < 3) { zm += 12; zy--; }
        int zk = zy % 100, zj = zy / 100;
        int zh = (zd + (13*(zm+1)/5) + zk + zk/4 + zj/4 - 2*zj) % 7;
        if (zh < 0) zh += 7;
        int dow = (zh + 6) % 7; /* 0=Sun, 1=Mon, ..., 6=Sat */

        char clock_str[40];
        int ci = 0;

        /* Weekday */
        const char *day_name = days[dow];
        while (*day_name) clock_str[ci++] = *day_name++;
        clock_str[ci++] = ','; clock_str[ci++] = ' ';

        /* Month */
        uint8_t mon = (dt.month > 0 && dt.month <= 12) ? dt.month - 1 : 0;
        const char *mon_name = months[mon];
        while (*mon_name) clock_str[ci++] = *mon_name++;
        clock_str[ci++] = ' ';

        /* Day */
        if (dt.day >= 10) clock_str[ci++] = '0' + dt.day / 10;
        clock_str[ci++] = '0' + dt.day % 10;
        clock_str[ci++] = ' '; clock_str[ci++] = ' ';

        /* HH:MM */
        u8_to_2dig(dt.hour, clock_str + ci); ci += 2;
        clock_str[ci++] = ':';
        u8_to_2dig(dt.minute, clock_str + ci); ci += 2;
        clock_str[ci] = '\0';

        int clock_len = strlenx(clock_str);
        int clock_x = w - clock_len * 8 - 12;
        gfx_surf_draw_string_smooth(&gs, clock_x, (MENUBAR_HEIGHT - 16) / 2,
                                   clock_str, ui_theme.text_primary, 1);
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

void menubar_update_windows(void) {
    menubar_paint();
}

int menubar_get_pill_x(int win_id) {
    for (int i = 0; i < pill_count; i++)
        if (pills[i].win_id == win_id)
            return pills[i].x + pills[i].w / 2;
    return -1;
}

/* ── Mouse ────────────────────────────────────────────────────────── */

int menubar_mouse(int mx, int my, int btn_down, int btn_up, int right_click) {
    (void)right_click;
    if (!bar) return 0;
    if (my >= MENUBAR_HEIGHT) return 0;

    /* Inside menubar: consume hover */
    if (!btn_up) return 1;

    /* Logo click → radial */
    if (mx >= logo_x && mx < logo_x + logo_w) {
        extern void radial_show(void);
        extern int  radial_visible(void);
        if (radial_visible()) {
            extern void radial_hide(void);
            radial_hide();
        } else {
            radial_show();
        }
        return 1;
    }

    /* Pill click → restore or bring to front */
    for (int i = 0; i < pill_count; i++) {
        if (mx >= pills[i].x && mx < pills[i].x + pills[i].w) {
            int wid = pills[i].win_id;
            ui_win_info_t info = ui_window_info(wid);
            if (info.w > 0) {
                if (info.state == UI_WIN_MINIMIZED)
                    ui_window_restore(wid);
                else
                    ui_window_raise(wid);
            }
            menubar_paint();
            return 1;
        }
    }

    return 1; /* consume all clicks in menubar */
    (void)btn_down;
}
