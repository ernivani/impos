/* radial.c — Radial app launcher: circular ring of pinned apps.
 *
 * Opened by Space key, closed by Escape or launch.
 * Icons arranged around a circle. Mouse hit-tests angle+distance.
 * Keyboard: arrows cycle ring, Enter launches, alphanumeric → drawer.
 */
#include <kernel/radial.h>
#include <kernel/compositor.h>
#include <kernel/app.h>
#include <kernel/icon_cache.h>
#include <kernel/gfx.h>
#include <string.h>

/* ── Geometry ───────────────────────────────────────────────────── */
#define OUTER_R   150   /* ring outer radius */
#define INNER_R   110   /* icon orbit radius */
#define CENTER_R   42   /* center circle radius */
#define ICON_SIZE  40   /* icon square size */

/* ── Integer trig (shared Bhaskara I) ──────────────────────────── */
static int bh_sin(int x) {
    if (x <= 0 || x >= 128) return 0;
    int n = 16 * x * (128 - x);
    int d = 81920 - 4 * x * (128 - x);
    return d ? n * 127 / d : 127;
}
static int isin2(int ph) {
    ph = ((ph % 256) + 256) % 256;
    return (ph < 128) ? bh_sin(ph) : -bh_sin(ph - 128);
}
static int icos2(int ph) { return isin2(ph + 64); }

/* Integer atan2: returns angle in [0, 255] for the full circle */
static int iatan2(int dy, int dx) {
    /* Approximate atan2 with octant decomposition */
    /* Returns value in [0, 255] = [0, 2*PI) */
    int angle;
    if (dx == 0 && dy == 0) return 0;

    /* Get absolute values */
    int ax = dx < 0 ? -dx : dx;
    int ay = dy < 0 ? -dy : dy;

    /* Octant 0: 0..PI/4 (dx>0, dy>=0, ax>=ay) */
    if (ax >= ay) {
        /* angle in [0, 32] (0 to PI/4) */
        angle = 32 * ay / (ax + 1);
    } else {
        /* angle in [32, 64] (PI/4 to PI/2) */
        angle = 64 - 32 * ax / (ay + 1);
    }

    /* Adjust for quadrant */
    if (dx >= 0 && dy >= 0) return angle;           /* Q1: 0..64 */
    if (dx < 0  && dy >= 0) return 128 - angle;     /* Q2: 64..128 */
    if (dx < 0  && dy < 0)  return 128 + angle;     /* Q3: 128..192 */
    return 256 - angle;                              /* Q4: 192..256 */
}

/* ── State ──────────────────────────────────────────────────────── */
static comp_surface_t *surf = 0;
static int vis = 0;
static int cx, cy;           /* center in screen coords */
static int hover_slot = -1;  /* pinned app slot under cursor, -1=center */
static int kb_slot = -1;     /* keyboard-selected slot */

static int is_open = 0;      /* actual display state */

/* ── Helpers ────────────────────────────────────────────────────── */

/* Angle (0-255) for pin slot n of N total */
static int slot_angle(int slot, int n) {
    if (n <= 0) return 0;
    /* Start from top (angle 192 = -90deg = up), go clockwise */
    return (192 + slot * 256 / n) & 255;
}

/* Icon center in surface-local coords */
static void slot_pos(int slot, int n, int *ox, int *oy) {
    int ang = slot_angle(slot, n);
    *ox = surf->w / 2 + icos2(ang) * INNER_R / 127;
    *oy = surf->h / 2 + isin2(ang) * INNER_R / 127;
}

/* ── Drawing ────────────────────────────────────────────────────── */

static void draw_circle_outline(uint32_t *px, int pw, int ph,
                                 int cx_, int cy_, int r, int thickness,
                                 uint32_t color) {
    int r_out = r, r_in = r - thickness;
    if (r_in < 0) r_in = 0;
    for (int y = cy_ - r_out; y <= cy_ + r_out; y++) {
        if (y < 0 || y >= ph) continue;
        for (int x = cx_ - r_out; x <= cx_ + r_out; x++) {
            if (x < 0 || x >= pw) continue;
            int dx = x - cx_, dy = y - cy_;
            int d2 = dx*dx + dy*dy;
            if (d2 >= r_in*r_in && d2 <= r_out*r_out)
                px[y * pw + x] = color;
        }
    }
}

static void draw_filled_circle(uint32_t *px, int pw, int ph,
                                int cx_, int cy_, int r, uint32_t color) {
    for (int y = cy_ - r; y <= cy_ + r; y++) {
        if (y < 0 || y >= ph) continue;
        for (int x = cx_ - r; x <= cx_ + r; x++) {
            if (x < 0 || x >= pw) continue;
            int dx = x - cx_, dy = y - cy_;
            if (dx*dx + dy*dy <= r*r)
                px[y * pw + x] = color;
        }
    }
}

static void alpha_blend_pixel(uint32_t *p, uint32_t color, int alpha) {
    int sr = (color >> 16) & 0xFF;
    int sg = (color >>  8) & 0xFF;
    int sb =  color        & 0xFF;
    int dr = (*p >> 16) & 0xFF;
    int dg = (*p >>  8) & 0xFF;
    int db =  *p        & 0xFF;
    int nr = dr + (sr - dr) * alpha / 255;
    int ng = dg + (sg - dg) * alpha / 255;
    int nb = db + (sb - db) * alpha / 255;
    *p = 0xFF000000 | ((uint32_t)nr << 16) | ((uint32_t)ng << 8) | (uint32_t)nb;
}

/* Draw a wedge highlight for the given slot */
static void draw_wedge(uint32_t *px, int pw, int ph,
                        int bcx, int bcy, int slot, int n, uint32_t color) {
    if (n <= 0) return;
    int ang0 = slot_angle(slot, n);
    int ang1 = slot_angle(slot + 1, n);

    for (int y = bcy - OUTER_R; y <= bcy + OUTER_R; y++) {
        if (y < 0 || y >= ph) continue;
        for (int x = bcx - OUTER_R; x <= bcx + OUTER_R; x++) {
            if (x < 0 || x >= pw) continue;
            int dx = x - bcx, dy = y - bcy;
            int d2 = dx*dx + dy*dy;
            if (d2 > OUTER_R*OUTER_R || d2 < CENTER_R*CENTER_R) continue;

            int ang = iatan2(dy, dx);
            /* Check if angle falls within this wedge */
            int in_wedge;
            if (ang0 <= ang1) {
                in_wedge = (ang >= ang0 && ang < ang1);
            } else {
                /* Wraps around 256 */
                in_wedge = (ang >= ang0 || ang < ang1);
            }
            if (in_wedge)
                alpha_blend_pixel(&px[y*pw + x], color, 46);
        }
    }
}

static void radial_draw_content(void) {
    if (!surf) return;

    int sw = surf->w, sh = surf->h;
    uint32_t *px = surf->pixels;

    /* Clear to fully transparent */
    memset(px, 0, (size_t)sw * sh * 4);

    /* Dark overlay background */
    for (int i = 0; i < sw * sh; i++)
        px[i] = 0x59000000; /* ~35% opacity black */

    int bcx = sw / 2, bcy = sh / 2;
    int n_pins = app_pin_count();

    /* Ring fill */
    draw_filled_circle(px, sw, sh, bcx, bcy, OUTER_R, 0xC70C1626);

    /* Wedge highlight for hovered / keyboard-selected slot */
    int active_slot = kb_slot >= 0 ? kb_slot : hover_slot;
    if (active_slot >= 0 && active_slot < n_pins) {
        const app_info_t *ai = app_get(app_pin_get(active_slot));
        uint32_t wc = ai ? ai->color : 0xFF3478F6;
        draw_wedge(px, sw, sh, bcx, bcy, active_slot, n_pins, wc);
    }

    /* Ring outline */
    draw_circle_outline(px, sw, sh, bcx, bcy, OUTER_R, 2, 0x14FFFFFF);

    /* Slice dividers */
    for (int s = 0; s < n_pins; s++) {
        int ang = slot_angle(s, n_pins);
        int ex = bcx + icos2(ang) * OUTER_R / 127;
        int ey = bcy + isin2(ang) * OUTER_R / 127;
        /* Draw line from center to edge */
        int steps = OUTER_R;
        for (int step = CENTER_R; step <= steps; step++) {
            int lx = bcx + icos2(ang) * step / 127;
            int ly = bcy + isin2(ang) * step / 127;
            if (lx >= 0 && lx < sw && ly >= 0 && ly < sh)
                alpha_blend_pixel(&px[ly * sw + lx], 0xFFFFFFFF, 13);
        }
        (void)ex; (void)ey;
    }

    /* App icons in ring */
    for (int s = 0; s < n_pins; s++) {
        int idx = app_pin_get(s);
        if (idx < 0) continue;
        const app_info_t *ai = app_get(idx);
        if (!ai) continue;

        int ox, oy;
        slot_pos(s, n_pins, &ox, &oy);

        int ix = ox - ICON_SIZE / 2;
        int iy = oy - ICON_SIZE / 2;

        uint32_t bg = ai->color;
        uint32_t fg = 0xFFFFFFFF;

        /* Brighten hovered icon */
        if (s == active_slot) {
            int r = ((bg >> 16) & 0xFF) + 40; if (r > 255) r = 255;
            int g = ((bg >>  8) & 0xFF) + 40; if (g > 255) g = 255;
            int b = ( bg        & 0xFF) + 40; if (b > 255) b = 255;
            bg = 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }

        icon_draw(ai->icon_id, px, sw, ix, iy, ICON_SIZE, bg, fg);
    }

    /* Center circle */
    draw_filled_circle(px, sw, sh, bcx, bcy, CENTER_R, 0xFF0C1626);
    draw_circle_outline(px, sw, sh, bcx, bcy, CENTER_R, 1, 0x28FFFFFF);

    /* Center label */
    {
        const char *label = "All apps";
        int is_app = 0;
        if (active_slot >= 0 && active_slot < n_pins) {
            int idx = app_pin_get(active_slot);
            const app_info_t *ai = app_get(idx);
            if (ai) { label = ai->name; is_app = 1; }
        }
        (void)is_app;

        /* Draw label centered */
        int len = 0;
        const char *p = label;
        while (*p++) len++;
        int tx = bcx - len * 4;
        int ty = bcy - 8;
        gfx_surface_t gs;
        gs.buf = px; gs.w = sw; gs.h = sh; gs.pitch = sw;
        gfx_surf_draw_string(&gs, tx, ty, label, 0xFFCDD6F4, 0);
    }

    comp_surface_damage_all(surf);
}

/* ── Public API ─────────────────────────────────────────────────── */

void radial_init(void) {
    int sw = (int)gfx_width(), sh = (int)gfx_height();
    surf = comp_surface_create(sw, sh, COMP_LAYER_OVERLAY);
    if (surf) {
        comp_surface_set_visible(surf, 0);
        comp_surface_raise(surf);
    }
    cx = sw / 2; cy = sh / 2;
    vis = 0; hover_slot = -1; kb_slot = -1;
}

void radial_show(void) {
    if (!surf) return;
    int sw = (int)gfx_width(), sh = (int)gfx_height();
    cx = sw / 2; cy = sh / 2;
    hover_slot = -1; kb_slot = -1;
    vis = 1; is_open = 1;
    comp_surface_set_visible(surf, 1);
    radial_draw_content();
}

void radial_hide(void) {
    if (!surf) return;
    vis = 0; is_open = 0;
    comp_surface_set_visible(surf, 0);
    comp_surface_damage_all(surf);
}

int radial_visible(void) { return vis; }

void radial_paint(void) {
    if (vis) radial_draw_content();
}

int radial_mouse(int mx, int my, int btn_down, int btn_up, int right_click) {
    if (!vis || !surf) return 0;
    (void)right_click;

    int n_pins = app_pin_count();

    /* Local coords relative to center */
    int dx = mx - cx, dy = my - cy;
    int d2 = dx*dx + dy*dy;

    /* Check if inside ring */
    int in_ring = (d2 >= CENTER_R * CENTER_R && d2 <= OUTER_R * OUTER_R);
    int in_center = (d2 < CENTER_R * CENTER_R);

    /* Calculate hovered slot */
    int new_hover = -1;
    if (in_ring && n_pins > 0) {
        int ang = iatan2(dy, dx);
        /* Find which wedge */
        for (int s = 0; s < n_pins; s++) {
            int a0 = slot_angle(s, n_pins);
            int a1 = slot_angle(s + 1, n_pins);
            int in_wedge;
            if (a0 <= a1) in_wedge = (ang >= a0 && ang < a1);
            else          in_wedge = (ang >= a0 || ang < a1);
            if (in_wedge) { new_hover = s; break; }
        }
    }

    int needs_repaint = (new_hover != hover_slot);
    hover_slot = new_hover;
    kb_slot = -1; /* mouse overrides keyboard selection */

    if (needs_repaint) radial_draw_content();

    /* Click: outside ring → close. Center → open drawer. Ring → launch. */
    if (btn_up) {
        if (!in_ring && !in_center) {
            radial_hide();
            return 1;
        }
        if (in_center) {
            radial_hide();
            /* Signal: open drawer — handled in desktop.c via flag */
            extern void drawer_show(const char *);
            drawer_show(0);
            return 1;
        }
        if (in_ring && hover_slot >= 0) {
            int idx = app_pin_get(hover_slot);
            const app_info_t *ai = app_get(idx);
            radial_hide();
            if (ai) app_launch(ai->id);
            return 1;
        }
    }

    if (btn_down) return 1; /* consume mouse-down inside radial */
    /* Outside completely? */
    if (d2 > (OUTER_R + 30) * (OUTER_R + 30) && !btn_down) return 0;
    return 1;
}

int radial_key(char ch, int scancode) {
    if (!vis) return 0;
    (void)scancode;
    int n_pins = app_pin_count();

    if (ch == 27) { /* Escape */
        radial_hide();
        return 1;
    }
    if (ch == 13) { /* Enter */
        int slot = kb_slot >= 0 ? kb_slot : hover_slot;
        if (slot >= 0) {
            int idx = app_pin_get(slot);
            const app_info_t *ai = app_get(idx);
            radial_hide();
            if (ai) app_launch(ai->id);
        } else {
            /* Enter on center */
            radial_hide();
            extern void drawer_show(const char *);
            drawer_show(0);
        }
        return 1;
    }

    /* Arrow keys: raw scancodes from keyboard driver
       Left=75, Right=77, Up=72, Down=80 in XT set */
    if (scancode == 75 || ch == 'h') { /* left */
        if (n_pins > 0) {
            if (kb_slot < 0) kb_slot = 0;
            else kb_slot = (kb_slot - 1 + n_pins) % n_pins;
            hover_slot = -1;
            radial_draw_content();
        }
        return 1;
    }
    if (scancode == 77 || ch == 'l') { /* right */
        if (n_pins > 0) {
            if (kb_slot < 0) kb_slot = 0;
            else kb_slot = (kb_slot + 1) % n_pins;
            hover_slot = -1;
            radial_draw_content();
        }
        return 1;
    }

    /* Alphanumeric: close radial, open drawer with prefix */
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9')) {
        char prefill[2] = { ch, 0 };
        radial_hide();
        extern void drawer_show(const char *);
        drawer_show(prefill);
        return 1;
    }

    return 1; /* consume all keys while radial is open */
}
