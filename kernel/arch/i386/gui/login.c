#include <kernel/login.h>
#include <kernel/desktop.h>
#include <kernel/gfx.h>
#include <kernel/tty.h>
#include <kernel/user.h>
#include <kernel/hostname.h>
#include <kernel/fs.h>
#include <kernel/group.h>
#include <kernel/env.h>
#include <kernel/config.h>
#include <kernel/idt.h>
#include <kernel/io.h>
#include <kernel/mouse.h>
#include <kernel/acpi.h>
#include <kernel/ui_theme.h>
#include <string.h>
#include <stdio.h>

/* ═══ Helpers ════════════════════════════════════════════════════ */

static void busy_wait(int n) {
    for (volatile int i = 0; i < n; i++);
}

static void fmt2(char *dst, int val) {
    dst[0] = '0' + (val / 10) % 10;
    dst[1] = '0' + val % 10;
}

static uint32_t lerp_color(uint32_t a, uint32_t b, int t) {
    int r = (int)((a >> 16) & 0xFF) + (((int)((b >> 16) & 0xFF) - (int)((a >> 16) & 0xFF)) * t / 255);
    int g = (int)((a >> 8) & 0xFF) + (((int)((b >> 8) & 0xFF) - (int)((a >> 8) & 0xFF)) * t / 255);
    int bl = (int)(a & 0xFF) + (((int)(b & 0xFF) - (int)(a & 0xFF)) * t / 255);
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (bl < 0) bl = 0;
    if (bl > 255) bl = 255;
    return GFX_RGB(r, g, bl);
}

/* ═══ Gradient Wallpaper ═══════════════════════════════════════ */

static uint32_t grad_tl, grad_tr, grad_bl, grad_br;

static void draw_gradient(int w, int h) {
    grad_tl = GFX_RGB(100, 85, 90);
    grad_tr = GFX_RGB(75, 65, 85);
    grad_bl = GFX_RGB(170, 120, 100);
    grad_br = GFX_RGB(120, 85, 105);

    uint32_t *bb = gfx_backbuffer();
    uint32_t pitch4 = gfx_pitch() / 4;

    for (int y = 0; y < h; y++) {
        int vy = y * 255 / (h - 1);
        uint32_t left  = lerp_color(grad_tl, grad_bl, vy);
        uint32_t right = lerp_color(grad_tr, grad_br, vy);
        for (int x = 0; x < w; x++) {
            int hx = x * 255 / (w - 1);
            bb[y * pitch4 + x] = lerp_color(left, right, hx);
        }
    }
}

static void restore_gradient_rect(int rx, int ry, int rw, int rh, int sw, int sh) {
    uint32_t *bb = gfx_backbuffer();
    uint32_t pitch4 = gfx_pitch() / 4;
    for (int y = ry; y < ry + rh && y < sh; y++) {
        if (y < 0) continue;
        int vy = y * 255 / (sh - 1);
        uint32_t left  = lerp_color(grad_tl, grad_bl, vy);
        uint32_t right = lerp_color(grad_tr, grad_br, vy);
        for (int x = rx; x < rx + rw && x < sw; x++) {
            if (x < 0) continue;
            int hx = x * 255 / (sw - 1);
            bb[y * pitch4 + x] = lerp_color(left, right, hx);
        }
    }
}

/* ═══ Clock + Date (top-right) ════════════════════════════════ */

static const char *month_names[] = {
    "", "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};
static const char *wday_names[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};

static void draw_login_clock(int w) {
    datetime_t dt;
    config_get_datetime(&dt);

    char timebuf[6];
    fmt2(timebuf, dt.hour);
    timebuf[2] = ':';
    fmt2(timebuf + 3, dt.minute);
    timebuf[5] = '\0';

    int scale = 3;
    int tw = gfx_string_scaled_w(timebuf, scale);
    int tx = w - tw - 30;
    int ty = 24;
    gfx_draw_string_smooth(tx, ty, timebuf, GFX_RGB(255, 255, 255), scale);

    int y2 = dt.year, m2 = dt.month, d2 = dt.day;
    if (m2 < 3) { m2 += 12; y2--; }
    int dow = (d2 + 13 * (m2 + 1) / 5 + y2 + y2 / 4 - y2 / 100 + y2 / 400) % 7;
    dow = (dow + 6) % 7;

    char datebuf[64];
    const char *mn = (dt.month >= 1 && dt.month <= 12) ? month_names[dt.month] : "???";
    const char *dn = (dow >= 0 && dow <= 6) ? wday_names[dow] : "???";
    snprintf(datebuf, sizeof(datebuf), "%s, %s %d", dn, mn, dt.day);

    int dw = (int)strlen(datebuf) * FONT_W;
    int dy = ty + FONT_H * scale + 6;
    gfx_draw_string_nobg(w - dw - 30, dy, datebuf, GFX_RGB(220, 220, 230));
}

/* ═══ Avatar ═══════════════════════════════════════════════════ */

#define AVATAR_R  40

static void draw_avatar(int cx, int cy) {
    int r = AVATAR_R;

    gfx_circle_ring(cx, cy, r + 3, 2, GFX_RGB(180, 170, 175));
    gfx_fill_circle_aa(cx, cy, r, GFX_RGB(95, 90, 105));

    int head_r = r * 5 / 16;
    int head_y = cy - r / 5;
    gfx_fill_circle_aa(cx, head_y, head_r, GFX_RGB(160, 155, 170));

    int body_top = cy + r / 6;
    int body_w = r * 3 / 4;
    for (int dy = 0; dy < r * 3 / 4; dy++) {
        int half_w = body_w - dy * dy / (r * 3 / 4);
        if (half_w < 4) half_w = 4;
        for (int dx = -half_w; dx <= half_w; dx++) {
            int px = cx + dx, py = body_top + dy;
            int d2 = (px - cx) * (px - cx) + (py - cy) * (py - cy);
            if (d2 <= (r - 2) * (r - 2))
                gfx_put_pixel(px, py, GFX_RGB(160, 155, 170));
        }
    }
}

/* ═══ macOS-style Action Bar ══════════════════════════════════ */

#define ACTION_COUNT    3
#define ACTION_BAR_H    32
#define ACTION_ITEM_PAD 20   /* horizontal padding per side of each label */
#define ACTION_DIV_W    1    /* divider width */

static const char *action_labels[ACTION_COUNT] = {
    "Sleep", "Restart", "Shut Down"
};

/* Layout state shared between draw and interaction */
static int screen_w, screen_h;
static int action_item_x[ACTION_COUNT];   /* left x of each item's hit region */
static int action_item_w[ACTION_COUNT];   /* width of each item's hit region */
static int action_pill_x, action_pill_y, action_pill_w, action_pill_h;
static int pw_field_x, pw_field_y;

static void compute_action_layout(int w, int h) {
    int total_items_w = 0;
    for (int i = 0; i < ACTION_COUNT; i++) {
        action_item_w[i] = (int)strlen(action_labels[i]) * FONT_W + ACTION_ITEM_PAD * 2;
        total_items_w += action_item_w[i];
    }
    total_items_w += (ACTION_COUNT - 1) * ACTION_DIV_W;

    int bar_pad = 6;
    action_pill_w = total_items_w + bar_pad * 2;
    action_pill_h = ACTION_BAR_H;
    action_pill_x = w / 2 - action_pill_w / 2;
    action_pill_y = h - 28 - ACTION_BAR_H;

    int x = action_pill_x + bar_pad;
    for (int i = 0; i < ACTION_COUNT; i++) {
        action_item_x[i] = x;
        x += action_item_w[i];
        if (i < ACTION_COUNT - 1) x += ACTION_DIV_W;
    }
}

#define ACTION_BAR_BG     GFX_RGB(0x2a, 0x28, 0x30)
#define ACTION_BAR_ALPHA  160
#define ACTION_DIV_COL    GFX_RGB(0x50, 0x4e, 0x58)
#define ACTION_HOVER_COL  GFX_RGB(0x48, 0x46, 0x50)
#define ACTION_TEXT_COL   GFX_RGB(0xd0, 0xcc, 0xda)
#define ACTION_TEXT_HI    GFX_RGB(0xff, 0xff, 0xff)

static void draw_action_bar(int w, int h, int hover_idx) {
    compute_action_layout(w, h);

    int pill_r = ACTION_BAR_H / 2;
    gfx_rounded_rect_alpha(action_pill_x, action_pill_y,
                           action_pill_w, action_pill_h, pill_r,
                           ACTION_BAR_BG, ACTION_BAR_ALPHA);

    /* Subtle top border */
    gfx_rounded_rect_outline(action_pill_x, action_pill_y,
                             action_pill_w, action_pill_h, pill_r,
                             GFX_RGB(0x55, 0x52, 0x5e));

    int text_y = action_pill_y + (ACTION_BAR_H - FONT_H) / 2;

    for (int i = 0; i < ACTION_COUNT; i++) {
        int ix = action_item_x[i];
        int iw = action_item_w[i];

        /* Hover highlight */
        if (i == hover_idx) {
            int hr = (ACTION_BAR_H - 8) / 2;
            gfx_rounded_rect_alpha(ix + 2, action_pill_y + 4,
                                   iw - 4, ACTION_BAR_H - 8, hr,
                                   ACTION_HOVER_COL, 180);
        }

        /* Label text centered in item region */
        int tw = (int)strlen(action_labels[i]) * FONT_W;
        int tx = ix + (iw - tw) / 2;
        uint32_t tc = (i == hover_idx) ? ACTION_TEXT_HI : ACTION_TEXT_COL;
        gfx_draw_string_nobg(tx, text_y, action_labels[i], tc);

        /* Vertical divider after item (except last) */
        if (i < ACTION_COUNT - 1) {
            int div_x = ix + iw;
            int div_y0 = action_pill_y + 8;
            int div_y1 = action_pill_y + ACTION_BAR_H - 8;
            for (int dy = div_y0; dy < div_y1; dy++)
                gfx_put_pixel(div_x, dy, ACTION_DIV_COL);
        }
    }
}

/* ═══ Network Icon (bottom-right) ═════════════════════════════ */

static void draw_wifi_icon(int x, int y, uint32_t color) {
    int r3 = 10;
    for (int dy = -r3; dy <= 0; dy++)
        for (int dx = -r3; dx <= r3; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 > r3 * r3 || d2 < (r3 - 2) * (r3 - 2)) continue;
            if (dy > -3) continue;
            gfx_put_pixel(x + dx, y + dy, color);
        }
    int r2 = 6;
    for (int dy = -r2; dy <= 0; dy++)
        for (int dx = -r2; dx <= r2; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 > r2 * r2 || d2 < (r2 - 2) * (r2 - 2)) continue;
            if (dy > -2) continue;
            gfx_put_pixel(x + dx, y + dy, color);
        }
    gfx_fill_circle_aa(x, y + 2, 2, color);
}

/* ═══ Password Field ═════════════════════════════════════════ */

#define PW_FIELD_W  240
#define PW_FIELD_H  36
#define PW_DOT_R    4
#define PW_DOT_GAP  14

static void draw_pw_field(int fx, int fy, int pw_len, int focused,
                          const char *err, int show_cursor) {
    int r = PW_FIELD_H / 2;

    /* Semi-transparent background, no outline */
    gfx_rounded_rect_alpha(fx, fy, PW_FIELD_W, PW_FIELD_H, r,
                           GFX_RGB(0, 0, 0), 100);

    int cx = fx + PW_FIELD_W / 2;
    int cy = fy + PW_FIELD_H / 2;

    if (pw_len == 0) {
        const char *ph = "Enter Password";
        int pw = (int)strlen(ph) * FONT_W;
        gfx_draw_string_nobg(cx - pw / 2, cy - FONT_H / 2,
                             ph, GFX_RGB(140, 135, 155));
    } else {
        int dots_w = pw_len * PW_DOT_GAP - (PW_DOT_GAP - PW_DOT_R * 2);
        int start_x = cx - dots_w / 2;
        for (int i = 0; i < pw_len && i < (PW_FIELD_W - 24) / PW_DOT_GAP; i++)
            gfx_fill_circle_aa(start_x + i * PW_DOT_GAP, cy, PW_DOT_R,
                               GFX_RGB(230, 225, 240));

        /* Blinking cursor only when typing */
        if (show_cursor && focused) {
            int visible_dots = pw_len;
            if (visible_dots > (PW_FIELD_W - 24) / PW_DOT_GAP)
                visible_dots = (PW_FIELD_W - 24) / PW_DOT_GAP;
            int cursor_x = start_x + (visible_dots - 1) * PW_DOT_GAP + PW_DOT_R + 4;
            gfx_fill_rect(cursor_x, cy - 7, 2, 14, GFX_RGB(200, 195, 220));
        }
    }

    if (err && err[0]) {
        int ew = (int)strlen(err) * FONT_W;
        gfx_draw_string_nobg(cx - ew / 2, fy + PW_FIELD_H + 10,
                             err, GFX_RGB(243, 139, 168));
    }
}

/* ═══ Fade Out ══════════════════════════════════════════════ */

static void login_fade_out(int x, int y, int w, int h, int steps, int delay_ms) {
    for (int i = 0; i <= steps; i++) {
        gfx_flip_rect(x, y, w, h);
        if (i < steps) {
            uint32_t *fb = gfx_framebuffer();
            uint32_t pitch4 = gfx_pitch() / 4;
            int x0 = x < 0 ? 0 : x;
            int y0 = y < 0 ? 0 : y;
            int x1 = x + w;
            if (x1 > (int)gfx_width()) x1 = (int)gfx_width();
            int y1 = y + h;
            if (y1 > (int)gfx_height()) y1 = (int)gfx_height();
            uint32_t inv_a = 255 - (uint32_t)(i * 255 / steps);
            for (int row = y0; row < y1; row++) {
                uint32_t *dst = fb + row * pitch4 + x0;
                for (int col = 0; col < x1 - x0; col++) {
                    uint32_t px = dst[col];
                    uint32_t r2 = ((px >> 16) & 0xFF) * inv_a / 255;
                    uint32_t g = ((px >> 8) & 0xFF) * inv_a / 255;
                    uint32_t b = (px & 0xFF) * inv_a / 255;
                    dst[col] = (r2 << 16) | (g << 8) | b;
                }
            }
        }
        pit_sleep_ms(delay_ms);
    }
}

/* ═══ Splash Screen ═════════════════════════════════════════ */

static void draw_spaced(int x, int y, const char *s, uint32_t fg, int sp) {
    while (*s) {
        gfx_draw_char_nobg(x, y, *s, fg);
        x += FONT_W + sp;
        s++;
    }
}

static int spaced_width(const char *s, int sp) {
    int n = (int)strlen(s);
    return n > 0 ? n * FONT_W + (n - 1) * sp : 0;
}

static void draw_spin_ring(int cx, int cy, int r, int thick, int frame) {
    int ro2 = r * r, ri2 = (r - thick) * (r - thick);
    int bright = frame % 8;
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++) {
            int d = dx * dx + dy * dy;
            if (d > ro2 || d < ri2) continue;
            int ax = dx >= 0 ? dx : -dx;
            int ay = dy >= 0 ? dy : -dy;
            int oct;
            if (dy < 0) {
                oct = dx >= 0 ? (ax < ay ? 0 : 1) : (ax < ay ? 7 : 6);
            } else {
                oct = dx >= 0 ? (ax < ay ? 3 : 2) : (ax < ay ? 4 : 5);
            }
            int dist = (oct - bright + 8) % 8;
            int b;
            if (dist == 0)      b = 128;
            else if (dist <= 2) b = 50;
            else                b = 20;
            gfx_put_pixel(cx + dx, cy + dy, GFX_RGB(b, b, b));
        }
}

void login_show_splash(void) {
    int w = (int)gfx_width(), h = (int)gfx_height();
    const char *logo = "IMPOS";
    int sp = 8;
    int lw = spaced_width(logo, sp);
    int lx = w / 2 - lw / 2;
    int ly = h / 2 - FONT_H / 2 - 16;
    int spin_cx = w / 2, spin_cy = ly + FONT_H + 32, spin_r = 14;

    gfx_clear(0);
    gfx_flip();

    for (int i = 0; i < 6; i++) {
        gfx_clear(0);
        int b = 38 * (i + 1);
        if (b > 230) b = 230;
        draw_spaced(lx, ly, logo, GFX_RGB(b, b, b), sp);
        gfx_flip();
        busy_wait(2200000);
    }
    for (int i = 0; i < 14; i++) {
        gfx_clear(0);
        draw_spaced(lx, ly, logo, GFX_RGB(230, 230, 230), sp);
        draw_spin_ring(spin_cx, spin_cy, spin_r, 2, i);
        gfx_flip();
        busy_wait(2200000);
    }
    gfx_clear(0);
    draw_spaced(lx, ly, logo, GFX_RGB(230, 230, 230), sp);
    login_fade_out(0, 0, w, h, 8, 40);
    gfx_clear(0);
    gfx_flip();
    pit_sleep_ms(100);
}

/* ═══ Setup Wizard ══════════════════════════════════════════ */

#define CARD_BG GFX_RGB(24, 24, 37)
#define FIELD_H 28

static int input_field(int fx, int fy, int fw,
                       char *buf, int max_len, int is_pw,
                       const char *ph) {
    int len = 0;
    buf[0] = '\0';
    while (1) {
        gfx_fill_rect(fx, fy, fw, FIELD_H, DT_FIELD_BG);
        gfx_draw_rect(fx, fy, fw, FIELD_H, len > 0 ? DT_FIELD_FOCUS : DT_FIELD_BORDER);
        int tx = fx + 10, ty = fy + (FIELD_H - FONT_H) / 2;
        if (len == 0 && ph)
            gfx_draw_string(tx, ty, ph, DT_FIELD_PH, DT_FIELD_BG);
        else if (is_pw) {
            for (int i = 0; i < len && i < (fw - 30) / 12; i++)
                gfx_fill_circle(tx + i * 12 + 4, fy + FIELD_H / 2, 3, DT_TEXT);
        } else {
            for (int i = 0; i < len; i++)
                gfx_draw_char(tx + i * FONT_W, ty, buf[i], DT_TEXT, DT_FIELD_BG);
        }
        int cx = is_pw ? tx + len * 12 : tx + len * FONT_W;
        gfx_fill_rect(cx, ty, 1, FONT_H, DT_DOT);
        gfx_flip();
        char c = getchar();
        if (c == '\n') { buf[len] = '\0'; return len; }
        if (c == '\b') { if (len > 0) len--; continue; }
        if (c == KEY_ESCAPE) { buf[len] = '\0'; return -1; }
        if ((unsigned char)c >= 0xB0 && (unsigned char)c <= 0xBB) continue;
        if (c >= 32 && c < 127 && len < max_len - 1)
            buf[len++] = c;
    }
}

static void setup_card(int cx, int cy, int cw, int ch) {
    int x = cx - cw / 2, y = cy - ch / 2;
    gfx_fill_rect(x, y, cw, ch, CARD_BG);
    gfx_draw_rect(x, y, cw, ch, DT_BORDER);
}

void login_run_setup(void) {
    int w = (int)gfx_width(), h = (int)gfx_height();
    int cx = w / 2, cy = h / 2;
    int cw = 300, ch = 200;
    int card_x = cx - cw / 2, card_y = cy - ch / 2;
    int fw = cw - 60, fx = card_x + 30;
    char hname[64] = {0}, rpw[64] = {0}, uname[32] = {0}, upw[64] = {0};

    gfx_clear(0);
    setup_card(cx, cy, cw, ch);
    const char *t1 = "Welcome";
    gfx_draw_string(cx - (int)strlen(t1) * FONT_W / 2, card_y + 28, t1, DT_TEXT, CARD_BG);
    gfx_draw_string(fx, card_y + 68, "Hostname", DT_TEXT_SUB, CARD_BG);
    gfx_flip();
    input_field(fx, card_y + 88, fw, hname, 64, 0, "imposos");
    if (!hname[0]) strcpy(hname, "imposos");
    hostname_set(hname);
    hostname_save();

    gfx_clear(0);
    setup_card(cx, cy, cw, ch);
    gfx_draw_string(cx - 52, card_y + 28, "Root Account", DT_TEXT, CARD_BG);
    gfx_draw_string(fx, card_y + 68, "Password", DT_TEXT_SUB, CARD_BG);
    gfx_flip();
    input_field(fx, card_y + 88, fw, rpw, 64, 1, "Password");
    fs_create_file("/home", 1);
    fs_create_file("/home/root", 1);
    user_create("root", rpw, "/home/root", 0, 0);
    user_create_home_dirs("/home/root");

    gfx_clear(0);
    setup_card(cx, cy, cw, ch + 80);
    gfx_draw_string(cx - 52, card_y + 18, "Your Account", DT_TEXT, CARD_BG);
    gfx_draw_string(fx, card_y + 52, "Username", DT_TEXT_SUB, CARD_BG);
    gfx_flip();
    input_field(fx, card_y + 72, fw, uname, 32, 0, "username");
    if (!uname[0]) strcpy(uname, "user");

    gfx_clear(0);
    setup_card(cx, cy, cw, ch + 80);
    gfx_draw_string(cx - 52, card_y + 18, "Your Account", DT_TEXT, CARD_BG);
    gfx_draw_string(fx, card_y + 52, "Username", DT_TEXT_SUB, CARD_BG);
    gfx_fill_rect(fx, card_y + 72, fw, FIELD_H, DT_FIELD_BG);
    gfx_draw_rect(fx, card_y + 72, fw, FIELD_H, DT_FIELD_BORDER);
    gfx_draw_string(fx + 10, card_y + 72 + (FIELD_H - FONT_H) / 2, uname, DT_TEXT, DT_FIELD_BG);
    gfx_draw_string(fx, card_y + 118, "Password", DT_TEXT_SUB, CARD_BG);
    gfx_flip();
    input_field(fx, card_y + 138, fw, upw, 64, 1, "Password");

    char uhome[128];
    snprintf(uhome, sizeof(uhome), "/home/%s", uname);
    fs_create_file(uhome, 1);
    uint16_t uid = 1000, gid = 1000;
    user_create(uname, upw, uhome, uid, gid);
    user_create_home_dirs(uhome);
    group_create(uname, gid);
    group_add_member(gid, uname);
    fs_chown(uhome, uid, gid);
    user_save();
    group_save();
    fs_sync();
    user_set_current(uname);
    fs_change_directory(uhome);
}

/* ═══ Hover / Click Detection ═══════════════════════════════ */

#define HOVER_NONE       0
#define HOVER_BTN_SLEEP  1
#define HOVER_BTN_RESTART 2
#define HOVER_BTN_SHUT   3
#define HOVER_PW_FIELD   4

static int get_hover_target(int mx, int my) {
    /* Check action bar items (rectangular hit test) */
    if (my >= action_pill_y && my < action_pill_y + action_pill_h) {
        for (int i = 0; i < ACTION_COUNT; i++) {
            if (mx >= action_item_x[i] &&
                mx < action_item_x[i] + action_item_w[i])
                return HOVER_BTN_SLEEP + i;
        }
    }

    /* Check password field */
    if (mx >= pw_field_x && mx < pw_field_x + PW_FIELD_W &&
        my >= pw_field_y && my < pw_field_y + PW_FIELD_H)
        return HOVER_PW_FIELD;

    return HOVER_NONE;
}

/* ═══ Full Login Screen ═════════════════════════════════════ */

#define NAME_GAP  14
#define PW_GAP    16

/* Shared state for idle callback */
static volatile int login_hover = HOVER_NONE;
static volatile int login_pw_len = 0;
static volatile int login_blink_on = 1;
static volatile uint32_t login_last_blink = 0;
static volatile int login_clicked_action = -1;
static volatile uint8_t login_prev_buttons = 0;
static volatile int login_needs_btn_redraw = 0;

static void draw_full_login(int w, int h, int sel, int num, int pw_len,
                            const char *err, int hover_action) {
    draw_gradient(w, h);
    draw_login_clock(w);

    int avatar_cy = h * 2 / 5;
    draw_avatar(w / 2, avatar_cy);

    user_t *u = user_get_by_index(sel);
    if (u) {
        int name_y = avatar_cy + AVATAR_R + NAME_GAP;
        int nscale = 2;
        int nw = gfx_string_scaled_w(u->username, nscale);
        gfx_draw_string_smooth(w / 2 - nw / 2, name_y, u->username,
                               GFX_RGB(240, 240, 248), nscale);

        pw_field_y = name_y + FONT_H * nscale + PW_GAP;
        pw_field_x = w / 2 - PW_FIELD_W / 2;
        draw_pw_field(pw_field_x, pw_field_y, pw_len, 1, err, login_blink_on);

        if (num > 1) {
            const char *hint = "< Arrow keys to switch user >";
            int hw = (int)strlen(hint) * FONT_W;
            gfx_draw_string_nobg(w / 2 - hw / 2, pw_field_y + PW_FIELD_H + 20,
                                 hint, GFX_RGB(150, 145, 165));
        }
    }

    draw_action_bar(w, h, hover_action);
    draw_wifi_icon(w - 30, h - 24, GFX_RGB(190, 185, 205));
}

/* Redraw just the action bar area (for hover updates) */
static void redraw_action_bar_area(int hover_idx) {
    /* Restore gradient behind bar */
    restore_gradient_rect(action_pill_x - 2, action_pill_y - 2,
                          action_pill_w + 4, action_pill_h + 4,
                          screen_w, screen_h);

    /* Redraw bar */
    int pill_r = ACTION_BAR_H / 2;
    gfx_rounded_rect_alpha(action_pill_x, action_pill_y,
                           action_pill_w, action_pill_h, pill_r,
                           ACTION_BAR_BG, ACTION_BAR_ALPHA);
    gfx_rounded_rect_outline(action_pill_x, action_pill_y,
                             action_pill_w, action_pill_h, pill_r,
                             GFX_RGB(0x55, 0x52, 0x5e));

    int text_y = action_pill_y + (ACTION_BAR_H - FONT_H) / 2;
    for (int i = 0; i < ACTION_COUNT; i++) {
        int ix = action_item_x[i];
        int iw = action_item_w[i];

        if (i == hover_idx) {
            int hr = (ACTION_BAR_H - 8) / 2;
            gfx_rounded_rect_alpha(ix + 2, action_pill_y + 4,
                                   iw - 4, ACTION_BAR_H - 8, hr,
                                   ACTION_HOVER_COL, 180);
        }

        int tw = (int)strlen(action_labels[i]) * FONT_W;
        int tx = ix + (iw - tw) / 2;
        uint32_t tc = (i == hover_idx) ? ACTION_TEXT_HI : ACTION_TEXT_COL;
        gfx_draw_string_nobg(tx, text_y, action_labels[i], tc);

        if (i < ACTION_COUNT - 1) {
            int div_x = ix + iw;
            int div_y0 = action_pill_y + 8;
            int div_y1 = action_pill_y + ACTION_BAR_H - 8;
            for (int dy = div_y0; dy < div_y1; dy++)
                gfx_put_pixel(div_x, dy, ACTION_DIV_COL);
        }
    }

    gfx_flip_rect(action_pill_x - 2, action_pill_y - 2,
                  action_pill_w + 4, action_pill_h + 4);
}

static void login_idle(void) {
    if (!mouse_poll()) {
        /* Handle cursor blink (every 500ms = 50 ticks at 100Hz) */
        uint32_t now = pit_get_ticks();
        if (now - login_last_blink >= 50) {
            login_last_blink = now;
            login_blink_on = !login_blink_on;

            /* Redraw password field with updated blink state */
            restore_gradient_rect(pw_field_x - 4, pw_field_y - 2,
                                  PW_FIELD_W + 8, PW_FIELD_H + 16,
                                  screen_w, screen_h);
            draw_pw_field(pw_field_x, pw_field_y, login_pw_len, 1, NULL,
                          login_blink_on);
            gfx_flip_rect(pw_field_x - 4, pw_field_y - 2,
                          PW_FIELD_W + 8, PW_FIELD_H + 16);
            gfx_draw_mouse_cursor(mouse_get_x(), mouse_get_y());
        }
        return;
    }

    int mx = mouse_get_x(), my = mouse_get_y();
    uint8_t btns = mouse_get_buttons();

    /* Detect hover target */
    int new_hover = get_hover_target(mx, my);

    /* Update cursor shape based on hover */
    if (new_hover >= HOVER_BTN_SLEEP && new_hover <= HOVER_BTN_SHUT) {
        if (gfx_get_cursor_type() != GFX_CURSOR_HAND)
            gfx_set_cursor_type(GFX_CURSOR_HAND);
    } else if (new_hover == HOVER_PW_FIELD) {
        if (gfx_get_cursor_type() != GFX_CURSOR_TEXT)
            gfx_set_cursor_type(GFX_CURSOR_TEXT);
    } else {
        if (gfx_get_cursor_type() != GFX_CURSOR_ARROW)
            gfx_set_cursor_type(GFX_CURSOR_ARROW);
    }

    /* Detect hover change on action buttons → redraw */
    int old_btn_hover = -1;
    int new_btn_hover = -1;
    if (login_hover >= HOVER_BTN_SLEEP && login_hover <= HOVER_BTN_SHUT)
        old_btn_hover = login_hover - HOVER_BTN_SLEEP;
    if (new_hover >= HOVER_BTN_SLEEP && new_hover <= HOVER_BTN_SHUT)
        new_btn_hover = new_hover - HOVER_BTN_SLEEP;

    if (old_btn_hover != new_btn_hover)
        login_needs_btn_redraw = 1;

    login_hover = new_hover;

    /* Redraw hovered buttons if needed */
    if (login_needs_btn_redraw) {
        login_needs_btn_redraw = 0;
        redraw_action_bar_area(new_btn_hover);
    }

    /* Detect click (button down edge) */
    uint8_t click = (btns & MOUSE_BTN_LEFT) & ~login_prev_buttons;
    login_prev_buttons = btns;

    if (click) {
        if (new_hover == HOVER_BTN_SHUT)
            acpi_shutdown();
        /* Sleep/Restart: set flag for main loop to handle */
        if (new_hover >= HOVER_BTN_SLEEP && new_hover <= HOVER_BTN_SHUT)
            login_clicked_action = new_hover - HOVER_BTN_SLEEP;
    }

    gfx_draw_mouse_cursor(mx, my);
}

/* Shake animation */
static void shake_field(int pw_len) {
    int offsets[] = { -8, 8, -6, 6, -3, 3, 0 };
    for (int i = 0; i < 7; i++) {
        int sx = pw_field_x + offsets[i];
        restore_gradient_rect(pw_field_x - 12, pw_field_y - 2,
                              PW_FIELD_W + 24, PW_FIELD_H + 4,
                              screen_w, screen_h);
        draw_pw_field(sx, pw_field_y, pw_len, 1, NULL, 1);
        gfx_flip_rect(pw_field_x - 12, pw_field_y - 2,
                      PW_FIELD_W + 24, PW_FIELD_H + 4);
        pit_sleep_ms(35);
    }
}

int login_run(void) {
    int w = (int)gfx_width(), h = (int)gfx_height();
    screen_w = w;
    screen_h = h;
    int num = user_count();
    if (num <= 0) return -1;

    int sel = 0;
    for (int i = 0; i < num; i++) {
        user_t *u = user_get_by_index(i);
        if (u && u->uid != 0) { sel = i; break; }
    }

    char pw[64], err[64] = {0};

    /* Initialize layout geometry */
    compute_action_layout(w, h);
    login_blink_on = 1;
    login_last_blink = pit_get_ticks();
    login_clicked_action = -1;
    login_prev_buttons = 0;
    login_hover = HOVER_NONE;

    keyboard_set_idle_callback(login_idle);

    while (1) {
        int pl = 0;
        pw[0] = '\0';
        login_pw_len = 0;

        /* Full redraw */
        draw_full_login(w, h, sel, num, 0, err, -1);
        gfx_flip();
        gfx_draw_mouse_cursor(mouse_get_x(), mouse_get_y());

        err[0] = '\0';

        /* Password field position (must match draw_full_login layout) */
        user_t *u = user_get_by_index(sel);
        if (!u) return -1;

        /* Input loop */
        while (1) {
            /* Reset blink on each keystroke */
            login_blink_on = 1;
            login_last_blink = pit_get_ticks();

            /* Redraw password field */
            restore_gradient_rect(pw_field_x - 4, pw_field_y - 2,
                                  PW_FIELD_W + 8, PW_FIELD_H + 16,
                                  w, h);
            draw_pw_field(pw_field_x, pw_field_y, pl, 1, NULL, 1);
            gfx_flip_rect(pw_field_x - 4, pw_field_y - 2,
                          PW_FIELD_W + 8, PW_FIELD_H + 16);

            char c = getchar();

            /* Check if mouse clicked an action button */
            if (login_clicked_action >= 0) {
                login_clicked_action = -1;
                continue;  /* Action already handled in idle callback */
            }

            if (c == '\n') {
                pw[pl] = '\0';
                break;
            }
            if (c == '\b') {
                if (pl > 0) { pl--; login_pw_len = pl; }
                continue;
            }
            if (c == KEY_LEFT) {
                sel = (sel + num - 1) % num;
                goto redraw;
            }
            if (c == KEY_RIGHT) {
                sel = (sel + 1) % num;
                goto redraw;
            }
            if ((unsigned char)c >= 0xB0 && (unsigned char)c <= 0xBB) continue;
            if (c == KEY_ESCAPE) {
                pl = 0;
                login_pw_len = 0;
                continue;
            }
            if (c >= 32 && c < 127 && pl < 63) {
                pw[pl++] = c;
                login_pw_len = pl;
            }
        }

        /* Authenticate */
        {
            user_t *su = user_get_by_index(sel);
            if (su) {
                user_t *auth = user_authenticate(su->username, pw);
                if (auth) {
                    user_set_current(auth->username);
                    fs_change_directory(auth->home);
                    keyboard_set_idle_callback(0);
                    gfx_set_cursor_type(GFX_CURSOR_ARROW);
                    gfx_restore_mouse_cursor();
                    login_fade_out(0, 0, w, h, 6, 30);
                    return 0;
                }
            }
        }

        /* Wrong password — shake + error */
        shake_field(pl);
        strcpy(err, "Incorrect password");
        continue;

    redraw:
        continue;
    }
}
