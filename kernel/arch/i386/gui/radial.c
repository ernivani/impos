/* radial.c — Radial app launcher: circular ring of pinned apps.
 *
 * Opened by Space key, closed by Escape or launch.
 * Icons arranged around a circle. Mouse hit-tests angle+distance.
 * Keyboard: arrows cycle ring, Enter launches, alphanumeric → drawer.
 */
#include <kernel/radial.h>
#include <kernel/compositor.h>
#include <kernel/anim.h>
#include <kernel/app.h>
#include <kernel/icon_cache.h>
#include <kernel/gfx.h>
#include <kernel/ui_window.h>
#include <string.h>

/* ── Geometry ───────────────────────────────────────────────────── */
/* Mockup: R=150 (outer/orbit), IR=110 (center circle), ICON=46.
   We enlarge slightly so icons fit inside the ring band with padding. */
#define OUTER_R   170   /* ring outer radius */
#define INNER_R   130   /* icon orbit radius (ring band midpoint) */
#define CENTER_R   90   /* center circle radius */
#define ICON_SIZE  46   /* icon square size — matches mockup exactly */

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

/* Animation */
static int anim_alpha = 255;
static int anim_id = -1;
static int hiding = 0;       /* 1 = fade-out in progress */

/* ── Helpers ────────────────────────────────────────────────────── */

/* Angle (0-255) for pin slot n of N total */
static int slot_angle(int slot, int n) {
    if (n <= 0) return 0;
    /* Start from top (angle 192 = -90deg = up), go clockwise */
    return (192 + slot * 256 / n) & 255;
}

/* Icon center in surface-local coords — use midpoint of wedge */
static void slot_pos(int slot, int n, int *ox, int *oy) {
    int a0   = slot_angle(slot,     n);
    int a1   = slot_angle(slot + 1, n);
    int diff = ((a1 - a0) + 256) & 255;   /* always positive span */
    int ang  = (a0 + diff / 2) & 255;     /* midpoint of wedge    */
    *ox = surf->w / 2 + icos2(ang) * INNER_R / 127;
    *oy = surf->h / 2 + isin2(ang) * INNER_R / 127;
}

/* ── Drawing ────────────────────────────────────────────────────── */

static void alpha_blend_pixel(uint32_t *p, uint32_t color, int alpha);

static void draw_circle_outline(uint32_t *px, int pw, int ph,
                                 int cx_, int cy_, int r, int thickness,
                                 uint32_t color, int alpha) {
    int r_out = r, r_in = r - thickness;
    if (r_in < 0) r_in = 0;
    for (int y = cy_ - r_out; y <= cy_ + r_out; y++) {
        if (y < 0 || y >= ph) continue;
        for (int x = cx_ - r_out; x <= cx_ + r_out; x++) {
            if (x < 0 || x >= pw) continue;
            int dx = x - cx_, dy = y - cy_;
            int d2 = dx*dx + dy*dy;
            if (d2 >= r_in*r_in && d2 <= r_out*r_out)
                alpha_blend_pixel(&px[y * pw + x], color, alpha);
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

    gfx_surface_t gs;
    gs.buf = px; gs.w = sw; gs.h = sh; gs.pitch = sw;

    /* Clear to fully transparent */
    memset(px, 0, (size_t)sw * sh * 4);

    /* Dark overlay background — mockup: rgba(0,0,0,0.35) */
    for (int i = 0; i < sw * sh; i++)
        px[i] = 0x59000000;

    int bcx = sw / 2, bcy = sh / 2;
    int n_pins = app_pin_count();
    int active_slot = kb_slot >= 0 ? kb_slot : hover_slot;

    /* ── Glass ring with anti-aliased edges + gradient ── */
    {
        int ro = OUTER_R, ri = CENTER_R;
        int ro2 = ro * ro, ri2 = ri * ri;
        /* AA band widths (in d² space): 2px smooth edges */
        int ro_aa_inner = (ro - 2) * (ro - 2);
        int ri_aa_outer = (ri + 2) * (ri + 2);

        for (int y = bcy - ro; y <= bcy + ro; y++) {
            if (y < 0 || y >= sh) continue;
            for (int x = bcx - ro; x <= bcx + ro; x++) {
                if (x < 0 || x >= sw) continue;
                int dx = x - bcx, dy = y - bcy;
                int d2 = dx * dx + dy * dy;
                if (d2 > ro2 || d2 < ri2) continue;

                /* Base alpha: 80% (204/255) — glass-like, not opaque */
                int alpha = 200;

                /* Anti-alias outer edge */
                if (d2 > ro_aa_inner) {
                    int frac = 255 - (d2 - ro_aa_inner) * 255 / (ro2 - ro_aa_inner + 1);
                    if (frac < 0) frac = 0;
                    alpha = alpha * frac / 255;
                }
                /* Anti-alias inner edge */
                if (d2 < ri_aa_outer) {
                    int frac = 255 - (ri_aa_outer - d2) * 255 / (ri_aa_outer - ri2 + 1);
                    if (frac < 0) frac = 0;
                    alpha = alpha * frac / 255;
                }

                /* Glass gradient: top of ring slightly lighter (reflection) */
                int gy = y - (bcy - ro);
                int grad_t = gy * 255 / (2 * ro);
                /* Top: rgb(18,28,46)  Bottom: rgb(8,16,28) */
                int cr = 18 - (18 - 8) * grad_t / 255;
                int cg = 28 - (28 - 16) * grad_t / 255;
                int cb = 46 - (46 - 28) * grad_t / 255;
                uint32_t ring_col = 0xFF000000 |
                    ((uint32_t)cr << 16) | ((uint32_t)cg << 8) | (uint32_t)cb;
                alpha_blend_pixel(&px[y * sw + x], ring_col, alpha);
            }
        }

        /* Inner glow: subtle 2px bright ring at inner edge */
        draw_circle_outline(px, sw, sh, bcx, bcy, ri + 3, 2, 0xFFFFFFFF, 12);
        /* Outer ring outline — fine white edge */
        draw_circle_outline(px, sw, sh, bcx, bcy, ro, 1, 0xFFFFFFFF, 25);
    }

    /* ── Wedge highlight for hovered / keyboard-selected slot ── */
    if (active_slot >= 0 && active_slot < n_pins) {
        const app_info_t *ai = app_get(app_pin_get(active_slot));
        uint32_t wc = ai ? ai->color : 0xFF3478F6;
        draw_wedge(px, sw, sh, bcx, bcy, active_slot, n_pins, wc);
    }

    /* ── Subtle slice dividers (softer than before) ── */
    for (int s = 0; s < n_pins; s++) {
        int ang = slot_angle(s, n_pins);
        /* Start divider 6px inside ring edge, end 6px from outer */
        for (int step = CENTER_R + 6; step <= OUTER_R - 6; step++) {
            int lx = bcx + icos2(ang) * step / 127;
            int ly = bcy + isin2(ang) * step / 127;
            if (lx >= 0 && lx < sw && ly >= 0 && ly < sh)
                alpha_blend_pixel(&px[ly * sw + lx], 0xFFFFFFFF, 10);
        }
    }

    /* ── App icons in ring + name labels ── */
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

        /* Name label below icon */
        {
            const char *name = ai->name;
            int nlen = 0;
            const char *p = name;
            while (*p++) nlen++;
            int lx = ox - nlen * 4;
            int ly = oy + ICON_SIZE / 2 + 4;
            if (lx < 0) lx = 0;
            if (ly >= 0 && ly < sh - 16) {
                uint32_t label_fg = (s == active_slot) ? 0xE5FFFFFF : 0x66FFFFFF;
                gfx_surf_draw_string_smooth(&gs, lx, ly, name, label_fg, 1);
            }
        }
    }

    /* ── Center circle with AA edges + glass gradient ── */
    {
        int center_hovered = (active_slot < 0 && hover_slot == -1);
        int cr = CENTER_R;
        int cr2 = cr * cr;
        int cr_aa = (cr - 2) * (cr - 2);

        for (int y = bcy - cr; y <= bcy + cr; y++) {
            if (y < 0 || y >= sh) continue;
            for (int x = bcx - cr; x <= bcx + cr; x++) {
                if (x < 0 || x >= sw) continue;
                int dxc = x - bcx, dyc = y - bcy;
                int d2c = dxc * dxc + dyc * dyc;
                if (d2c > cr2) continue;

                int alpha = center_hovered ? 240 : 230;

                /* AA edge */
                if (d2c > cr_aa) {
                    int frac = 255 - (d2c - cr_aa) * 255 / (cr2 - cr_aa + 1);
                    if (frac < 0) frac = 0;
                    alpha = alpha * frac / 255;
                }

                /* Center gradient: top lighter for depth */
                int gy = y - (bcy - cr);
                int gt = gy * 255 / (2 * cr);
                int cr_, cg_, cb_;
                if (center_hovered) {
                    cr_ = 24 - (24 - 16) * gt / 255;
                    cg_ = 36 - (36 - 26) * gt / 255;
                    cb_ = 58 - (58 - 42) * gt / 255;
                } else {
                    cr_ = 14 - (14 - 6) * gt / 255;
                    cg_ = 22 - (22 - 12) * gt / 255;
                    cb_ = 36 - (36 - 20) * gt / 255;
                }
                uint32_t fill = 0xFF000000 |
                    ((uint32_t)cr_ << 16) | ((uint32_t)cg_ << 8) | (uint32_t)cb_;
                alpha_blend_pixel(&px[y * sw + x], fill, alpha);
            }
        }

        /* Center outline */
        int center_border_a = center_hovered ? 50 : 20;
        draw_circle_outline(px, sw, sh, bcx, bcy, cr, 1, 0xFFFFFFFF, center_border_a);
    }

    /* ── Center content ── */
    {
        if (active_slot >= 0 && active_slot < n_pins) {
            /* Show hovered app name */
            int idx = app_pin_get(active_slot);
            const app_info_t *ai = app_get(idx);
            if (ai) {
                int len = 0;
                const char *p = ai->name;
                while (*p++ && len < 9) len++;
                int tx = bcx - len * 4;
                int ty = bcy - 8;
                gfx_surf_draw_string_smooth(&gs, tx, ty, ai->name, 0xB3FFFFFF, 1);
            }
        } else {
            /* 2×2 dot grid, radius 3px, gap 13px */
            int dot_r = 3;
            int gap = 13;
            for (int row = 0; row < 2; row++) {
                for (int col = 0; col < 2; col++) {
                    int ddx = col * gap - gap / 2;
                    int ddy = row * gap - gap / 2 - 8;
                    int ddcx = bcx + ddx, ddcy = bcy + ddy;
                    /* AA dots */
                    int dr2 = dot_r * dot_r;
                    int dr_aa = (dot_r - 1) * (dot_r - 1);
                    for (int dy2 = -dot_r; dy2 <= dot_r; dy2++) {
                        for (int dx2 = -dot_r; dx2 <= dot_r; dx2++) {
                            int dd = dx2*dx2 + dy2*dy2;
                            if (dd > dr2) continue;
                            int da = 77;
                            if (dd > dr_aa)
                                da = da * (255 - (dd - dr_aa) * 255 / (dr2 - dr_aa + 1)) / 255;
                            int px2 = ddcx + dx2, py2 = ddcy + dy2;
                            if (px2 >= 0 && px2 < sw && py2 >= 0 && py2 < sh)
                                alpha_blend_pixel(&px[py2 * sw + px2],
                                                  0xFFFFFFFF, da);
                        }
                    }
                }
            }
            /* "All apps" label */
            const char *label = "All apps";
            int len = 8;
            int tx = bcx - len * 4;
            int ty = bcy + 10;
            gfx_surf_draw_string_smooth(&gs, tx, ty, label, 0x33FFFFFF, 1);
        }
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
    hiding = 0;
    vis = 1; is_open = 1;
    ui_window_set_all_visible(0);  /* hide windows behind overlay */
    if (anim_id >= 0) anim_cancel(anim_id);
    anim_alpha = 0;
    anim_id = anim_start(&anim_alpha, 0, 255, 180, ANIM_EASE_OUT);
    comp_surface_set_alpha(surf, 0);
    comp_surface_set_visible(surf, 1);
    radial_draw_content();
}

void radial_hide(void) {
    if (!surf) return;
    if (hiding) return; /* already animating out */
    hiding = 1;
    if (anim_id >= 0) anim_cancel(anim_id);
    anim_id = anim_start(&anim_alpha, anim_alpha, 0, 120, ANIM_EASE_IN);
    /* Actual hide happens in radial_tick() when animation completes */
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

void radial_tick(void) {
    if (anim_id < 0) return;
    if (!surf) return;
    comp_surface_set_alpha(surf, (uint8_t)(anim_alpha < 0 ? 0 : anim_alpha > 255 ? 255 : anim_alpha));
    if (!anim_active(anim_id)) {
        anim_id = -1;
        if (hiding) {
            hiding = 0;
            vis = 0; is_open = 0;
            comp_surface_set_visible(surf, 0);
            comp_surface_damage_all(surf);
            ui_window_set_all_visible(1);  /* restore windows */
        }
    }
}
