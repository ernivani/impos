/* wallpaper.c — Phase 6: Procedural wallpaper engine.
 *
 * 5 styles × up to 4 theme variants each.
 * All drawing is done in ARGB (0xFF000000 | RGB) pixel buffers.
 * Uses PIT ticks (120Hz) for animation time.
 * No libm required — uses integer sine approximation (Bhaskara I).
 */
#include <kernel/wallpaper.h>
#include <string.h>

/* ── Integer trig (Bhaskara I approximation) ───────────────────── */
/* isin(phase): phase 0-255 = 0 to 2*PI, returns -127 to +127       */

static int bhaskara(int x) {
    /* sin(x * PI / 128) * 127, x in [0, 128] */
    if (x <= 0)   return 0;
    if (x >= 128) return 0;
    int n = 16 * x * (128 - x);
    int d = 81920 - 4 * x * (128 - x);
    if (d == 0) return 127;
    return n * 127 / d;
}

static int isin(int phase) {
    phase = ((phase % 256) + 256) % 256;
    if (phase < 128) return  bhaskara(phase);
    else             return -bhaskara(phase - 128);
}

static int icos(int phase) { return isin(phase + 64); }

/* ── Color helpers ──────────────────────────────────────────────── */

static uint32_t mkrgb(int r, int g, int b) {
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    return 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static int clamp(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static uint32_t lerp_c(uint32_t ca, uint32_t cb, int t, int denom) {
    if (denom <= 0) return ca;
    int ra = (ca >> 16) & 0xFF, ga = (ca >> 8) & 0xFF, ba = ca & 0xFF;
    int rb = (cb >> 16) & 0xFF, gb = (cb >> 8) & 0xFF, bb = cb & 0xFF;
    return mkrgb(ra + (rb - ra) * t / denom,
                 ga + (gb - ga) * t / denom,
                 ba + (bb - ba) * t / denom);
}

/* Multi-stop vertical gradient: colors[n], y in [0, h) */
static uint32_t vgrad(int y, int h, const uint32_t *stops, int n) {
    if (n <= 0) return 0xFF000000;
    if (n == 1) return stops[0];
    int seg = (n - 1);
    int total = h;
    int si = y * seg / total;
    if (si >= seg) si = seg - 1;
    int seg_h = total / seg;
    int seg_y = y - si * seg_h;
    return lerp_c(stops[si], stops[si + 1], seg_y, seg_h);
}

/* Fast LCG pseudo-random */
static uint32_t lcg(uint32_t seed) {
    return seed * 1664525u + 1013904223u;
}

/* ── Theme definitions ───────────────────────────────────────────── */
/* Colors match the HTML mockup exactly.                               */

typedef struct {
    const char *name;
    uint32_t    dot_color;
    uint32_t    sky[5];       /* up to 5 sky gradient stops */
    int         sky_stops;
    uint32_t    accent1;      /* layer 0 mountain / wave / nebula color */
    uint32_t    accent2;      /* layer 1 */
    uint32_t    accent3;      /* layer 2 */
} theme_t;

/* Mountains: 4 themes — exact mockup colors */
static const theme_t mtns[4] = {
    /* Night: sky #0a0e1a→#1a1f3a, mtn #0c1020/#0e1225/#10152a */
    { "Night", 0xFF1A1F3A,
      { 0xFF0A0E1A, 0xFF0D1328, 0xFF121A35, 0xFF1A1F3A, 0 }, 4,
      0xFF0C1020, 0xFF0E1225, 0xFF10152A },
    /* Dawn: sky #1a1025→#e8a870, mtn #1a1020/#251828/#352030 */
    { "Dawn", 0xFFC87050,
      { 0xFF1A1025, 0xFF2D1530, 0xFF6B3040, 0xFFC87050, 0xFFE8A870 }, 5,
      0xFF1A1020, 0xFF251828, 0xFF352030 },
    /* Day: sky #2a6ac0→#b0d8ff, mtn #3a5570/#4a6580/#5a7590 */
    { "Day", 0xFF60A0E0,
      { 0xFF2A6AC0, 0xFF3A80D0, 0xFF60A0E0, 0xFF90C0F0, 0xFFB0D8FF }, 5,
      0xFF3A5570, 0xFF4A6580, 0xFF5A7590 },
    /* Dusk: sky #1a1028→#d09040, mtn #1a1020/#251520/#352025 */
    { "Dusk", 0xFFC06530,
      { 0xFF1A1028, 0xFF3A1830, 0xFF7A3535, 0xFFC06530, 0xFFD09040 }, 5,
      0xFF1A1020, 0xFF251520, 0xFF352025 },
};

/* Gradient: 4 themes — exact mockup colors */
static const theme_t grads[4] = {
    /* Sunset */
    { "Sunset", 0xFFD04020,
      { 0xFF1A0530, 0xFF6B1040, 0xFFD04020, 0xFFF08030, 0xFFFFD060 }, 5,
      0xFF6B1040, 0xFFD04020, 0xFFF08030 },
    /* Ocean */
    { "Ocean", 0xFF1060A0,
      { 0xFF020818, 0xFF0A2848, 0xFF1060A0, 0xFF20A0D0, 0xFF60D0E0 }, 5,
      0xFF0A2848, 0xFF1060A0, 0xFF20A0D0 },
    /* Aurora */
    { "Aurora", 0xFF40C080,
      { 0xFF0A1020, 0xFF103040, 0xFF10806A, 0xFF40C080, 0xFF80F0A0 }, 5,
      0xFF103040, 0xFF10806A, 0xFF40C080 },
    /* Midnight */
    { "Midnight", 0xFF401868,
      { 0xFF08060E, 0xFF150828, 0xFF281048, 0xFF401868, 0xFF602888 }, 5,
      0xFF150828, 0xFF281048, 0xFF401868 },
};

/* Geometric: 3 themes — exact mockup colors */
static const theme_t geos[3] = {
    /* Dark: bg #0a0c12, palette[0] used as sky base */
    { "Dark", 0xFF283050,
      { 0xFF0A0C12, 0xFF1A2030, 0xFF202840, 0xFF283050, 0 }, 4,
      0xFF303860, 0xFF384070, 0xFF283050 },
    /* Colorful: bg #10101a */
    { "Colorful", 0xFFA03060,
      { 0xFF10101A, 0xFF4030A0, 0xFFA03060, 0xFFD06020, 0xFF30A070 }, 5,
      0xFF4030A0, 0xFFA03060, 0xFFD06020 },
    /* Neon: bg #05050a */
    { "Neon", 0xFFFF0080,
      { 0xFF05050A, 0xFF0D0015, 0xFF001A0A, 0xFF000D1A, 0xFF100010 }, 5,
      0xFFFF0080, 0xFF00FF80, 0xFF0080FF },
};

/* Stars: 3 themes — exact mockup colors */
static const theme_t stars_t[3] = {
    /* Deep Space: bg #020208 */
    { "Deep Space", 0xFF1A1040,
      { 0xFF020208, 0xFF050510, 0xFF080818, 0xFF0A0A20, 0 }, 4,
      0xFF140A3C, 0xFF3C0A28, 0xFF0A0A28 },
    /* Nebula: bg #050210 */
    { "Nebula", 0xFF6020A0,
      { 0xFF050210, 0xFF0D0520, 0xFF140838, 0xFF1A1050, 0 }, 4,
      0xFF501478, 0xFF143C78, 0xFF78183C },
    /* Starfield: bg #000005 */
    { "Starfield", 0xFF101830,
      { 0xFF000005, 0xFF030308, 0xFF05050C, 0xFF080810, 0 }, 4,
      0xFF0A1428, 0xFF0A1428, 0xFF0A1428 },
};

/* Waves: 3 themes — exact mockup colors */
static const theme_t wavest[3] = {
    /* Ocean: sky #081828→#2090d0, waves #0a3060→#2080d0 */
    { "Ocean", 0xFF1860A0,
      { 0xFF081828, 0xFF103050, 0xFF1860A0, 0xFF2090D0, 0 }, 4,
      0xFF0A3060, 0xFF0C4080, 0xFF1050A0 },
    /* Sunset Sea: sky #1a0820→#e8a050, waves #301020→#a84040 */
    { "Sunset Sea", 0xFFD06030,
      { 0xFF1A0820, 0xFF501030, 0xFFA03030, 0xFFD06030, 0 }, 4,
      0xFF301020, 0xFF501828, 0xFF702030 },
    /* Arctic: sky #101820→#608090, waves #182830→#406878 */
    { "Arctic", 0xFF406070,
      { 0xFF101820, 0xFF182838, 0xFF284050, 0xFF406070, 0 }, 4,
      0xFF182830, 0xFF203840, 0xFF284850 },
};

/* ── State ──────────────────────────────────────────────────────── */

static int  cur_style = WALLPAPER_MOUNTAINS;
static int  cur_theme = 0;

/* ── Style: Mountains ───────────────────────────────────────────── */
/* Uses smooth sine-wave silhouettes matching the HTML mockup exactly. */

static void draw_mountains(uint32_t *buf, int w, int h, uint32_t t,
                            int style, int theme_idx) {
    const theme_t *th = &mtns[theme_idx];

    /* Sky gradient: up to 5 stops */
    for (int y = 0; y < h; y++) {
        uint32_t col = vgrad(y, h, th->sky, th->sky_stops);
        for (int x = 0; x < w; x++)
            buf[y * w + x] = col;
    }

    /* Stars: Night(idx=0) bright, Dawn(idx=1) faint */
    {
        int star_alpha = (theme_idx == 0) ? 160 : (theme_idx == 1) ? 50 : 0;
        if (star_alpha > 0) {
            uint32_t seed = 42;
            for (int i = 0; i < 120; i++) {
                seed = lcg(seed); int sx = (int)(seed % (uint32_t)w);
                seed = lcg(seed); int sy = (int)(seed % (uint32_t)(h * 6 / 10));
                seed = lcg(seed); int sb = (int)(seed % 256);
                int flick = isin((int)(t * 2 + sb)) * 40 / 127;
                int br = clamp(star_alpha + flick, 0, 255);
                buf[sy * w + sx] = mkrgb(br, br, br + 10);
            }
        }
    }

    /* Aurora: only night theme (idx=0) */
    if (theme_idx == 0) {
        for (int i = 0; i < 3; i++) {
            int ay_base = h * 15 / 100 + i * h * 35 / 1000;
            int aurora_amp = h * 3 / 100;
            for (int x = 0; x < w; x++) {
                int ph = (x * 5 * 128 / w + (int)(t / 2 + i * 40)) & 255;
                int ay = ay_base + isin(ph) * aurora_amp / 127;
                if (ay < 0 || ay >= h) continue;
                uint32_t *p = &buf[ay * w + x];
                int dr = (*p >> 16) & 0xFF;
                int dg = (*p >>  8) & 0xFF;
                int db =  *p        & 0xFF;
                /* green-cyan aurora at 15% opacity */
                buf[ay * w + x] = mkrgb(dr + (0   - dr) * 15 / 100,
                                        dg + (200 - dg) * 15 / 100,
                                        db + (150 - db) * 15 / 100);
            }
        }
    }

    /* Three sine-wave mountain layers (far to near).
       Algorithm matches the HTML mockup drawMountains() function:
         baseY = h*0.55 + layer*(h*0.033)
         hv = sin(nx*freq1*PI + layer*2)*40 + sin(nx*freq2*PI+layer)*20 + sin(nx*15*PI)*10
         top_y = baseY - hv*(1-layer*0.2)*(h/800)
       Phase mapping: nx*freq*PI in isin units = x*freq*128/w
       Phase offset: layer*2 rad ≈ layer*81 isin units; layer rad ≈ layer*40 units */
    for (int layer = 0; layer < 3; layer++) {
        uint32_t mcolor;
        if (layer == 0) mcolor = th->accent1;
        else if (layer == 1) mcolor = th->accent2;
        else mcolor = th->accent3;

        int base_y = h * 55 / 100 + layer * h * 33 / 1000;
        int freq1 = 3 + layer;
        int freq2 = 7 + layer * 2;

        for (int x = 0; x < w; x++) {
            /* Phase for each frequency component */
            int ph1 = (x * freq1 * 128 / w + layer * 81) & 255;
            int ph2 = (x * freq2 * 128 / w + layer * 40) & 255;
            int ph3 = (x * 15   * 128 / w)               & 255;

            /* Weighted sine sum — matches JS: *40 + *20 + *10 */
            int v = isin(ph1) * 40 + isin(ph2) * 20 + isin(ph3) * 10;

            /* Scale factor per layer: 1.0, 0.8, 0.6 → ×10, ×8, ×6 */
            int scale = 10 - layer * 2;

            /* Pixel offset = v * scale * h / (127 * 10 * 800) */
            int offset = v * scale * h / (127 * 10 * 800);
            int top_y  = base_y - offset;
            if (top_y < 0) top_y = 0;
            if (top_y >= h) continue;

            /* Fill column from silhouette to bottom */
            for (int y = top_y; y < h; y++)
                buf[y * w + x] = mcolor;
        }
    }

    (void)t; (void)style;
}

/* ── Style: Gradient ────────────────────────────────────────────── */

static void draw_gradient(uint32_t *buf, int w, int h, uint32_t t,
                           int style, int theme_idx) {
    const theme_t *th = &grads[theme_idx];

    /* Slowly drifting diagonal gradient — matches mockup drawGradient() */
    int angle = (int)(t / 2) & 255;
    int ax = icos(angle) * 80 / 127;   /* -80..+80 */
    int ay = isin(angle) * 80 / 127;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int proj = (x * (ax + w / 2) / w + y * (ay + h / 2) / h);
            proj = clamp(proj, 0, th->sky_stops - 1);
            /* Smooth interpolation across gradient stops */
            int total = w / 2 + h / 2;
            int raw = (x * (ax + w/2) / w + y * (ay + h/2) / h);
            raw = clamp(raw, 0, total);
            uint32_t col = vgrad(raw, total, th->sky, th->sky_stops);
            buf[y * w + x] = col;
        }
    }

    /* 3 soft orbs drifting slowly */
    static const int orb_x[3] = { 30, 65, 80 };
    static const int orb_y[3] = { 30, 70, 20 };
    static const int orb_r[3] = { 28, 22, 18 };

    uint32_t orb_cols[3] = { th->accent1, th->accent2, th->accent3 };

    for (int o = 0; o < 3; o++) {
        int drift = 8;
        int ph = (int)(t / 3 + o * 85) & 255;
        int cx = w * orb_x[o] / 100 + icos(ph) * drift / 127;
        int cy = h * orb_y[o] / 100 + isin(ph) * drift / 127;
        int radius = (w < h ? w : h) * orb_r[o] / 100;

        int xmin = cx - radius; if (xmin < 0) xmin = 0;
        int xmax = cx + radius; if (xmax > w) xmax = w;
        int ymin = cy - radius; if (ymin < 0) ymin = 0;
        int ymax = cy + radius; if (ymax > h) ymax = h;

        int or_ = (orb_cols[o] >> 16) & 0xFF;
        int og  = (orb_cols[o] >>  8) & 0xFF;
        int ob  =  orb_cols[o]        & 0xFF;

        for (int py = ymin; py < ymax; py++) {
            for (int px = xmin; px < xmax; px++) {
                int dx = px - cx, dy = py - cy;
                int d2 = dx*dx + dy*dy, r2 = radius * radius;
                if (d2 >= r2) continue;
                int alpha = 40 - 40 * d2 / r2;
                uint32_t *p = &buf[py * w + px];
                int dr = (*p >> 16) & 0xFF;
                int dg = (*p >>  8) & 0xFF;
                int db =  *p        & 0xFF;
                buf[py * w + px] = mkrgb(dr + (or_ - dr) * alpha / 255,
                                         dg + (og  - dg) * alpha / 255,
                                         db + (ob  - db) * alpha / 255);
            }
        }
    }
    (void)style;
}

/* ── Style: Geometric ───────────────────────────────────────────── */
/* Tessellated triangles with pulsing opacity, matching mockup drawGeometric(). */

static void draw_geometric(uint32_t *buf, int w, int h, uint32_t t,
                            int style, int theme_idx) {
    const theme_t *th = &geos[theme_idx];

    /* Base fill */
    for (int i = 0; i < w * h; i++) buf[i] = th->sky[0];

    /* Hex-offset triangle grid like mockup (sz≈60, 0.866 row pitch) */
    int sz = 60;
    int cols = w / sz + 2;
    int rows = h * 100 / 87 / sz + 2;  /* 0.866 ≈ 87/100 */

    /* Palette from mockup sky stops + accents */
    uint32_t palette[5] = {
        th->sky[1], th->sky[2], th->sky[3],
        th->accent1, th->accent2
    };
    int pal_count = 5;

    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            int ci = ((col * 7 + row * 13) % pal_count + pal_count) % pal_count;
            /* Pulsing: 0.15..0.30 opacity in mockup, here ≈ 38..77 alpha */
            int pulse = isin((int)(t + col * 8 + row * 13)) * 10 / 127;
            int alpha = 38 + pulse + 38; /* 38..76 */
            if (alpha < 0) alpha = 0;
            if (alpha > 100) alpha = 100;

            int cx = col * sz + (row & 1 ? sz / 2 : 0);
            int cy = row * sz * 87 / 100;

            /* Upper triangle: tip (cx,cy-sz*2/5), base at cy+sz*1/5 */
            int tx0 = cx, ty0 = cy - sz * 2 / 5;
            int ty1 = cy + sz / 5;
            int tx2 = cx - sz / 2;
            /* Lower triangle: inverted */
            int bx1 = cx + sz / 2, by1 = cy;
            int bx2 = cx - sz / 2;
            int bty = cy + sz * 2 / 5;

            uint32_t pc = palette[ci];
            int pr = (pc >> 16) & 0xFF;
            int pg = (pc >>  8) & 0xFF;
            int pb =  pc        & 0xFF;

            /* Fill both triangles with scanline rasterisation */
            for (int py = ty0; py <= ty1 && py < h; py++) {
                if (py < 0) continue;
                int span = (py - ty0) * (sz / 2) / (ty1 - ty0 + 1);
                for (int px = tx0 - span; px <= tx0 + span && px < w; px++) {
                    if (px < 0) continue;
                    uint32_t *p = &buf[py * w + px];
                    int dr = (*p >> 16) & 0xFF;
                    int dg = (*p >>  8) & 0xFF;
                    int db =  *p        & 0xFF;
                    buf[py * w + px] = mkrgb(dr + (pr - dr) * alpha / 100,
                                             dg + (pg - dg) * alpha / 100,
                                             db + (pb - db) * alpha / 100);
                }
            }
            for (int py = by1; py <= bty && py < h; py++) {
                if (py < 0) continue;
                int span = (bty - py) * (sz / 2) / (bty - by1 + 1);
                for (int px = bx2 + (sz/2 - span); px <= bx1 - (sz/2 - span) && px < w; px++) {
                    if (px < 0) continue;
                    uint32_t *p = &buf[py * w + px];
                    int dr = (*p >> 16) & 0xFF;
                    int dg = (*p >>  8) & 0xFF;
                    int db =  *p        & 0xFF;
                    buf[py * w + px] = mkrgb(dr + (pr - dr) * alpha / 100,
                                             dg + (pg - dg) * alpha / 100,
                                             db + (pb - db) * alpha / 100);
                }
            }
            (void)tx2; (void)bx2; (void)bx1; (void)bty;
        }
    }
    (void)style;
}

/* ── Style: Stars ───────────────────────────────────────────────── */

static void draw_stars_wp(uint32_t *buf, int w, int h, uint32_t t,
                           int style, int theme_idx) {
    const theme_t *th = &stars_t[theme_idx];

    /* Deep background */
    for (int y = 0; y < h; y++) {
        uint32_t col = vgrad(y, h, th->sky, th->sky_stops);
        for (int x = 0; x < w; x++)
            buf[y * w + x] = col;
    }

    /* Nebula clouds (3 radial blobs) */
    static const int neb_x[3] = { 30, 70, 50 };
    static const int neb_y[3] = { 40, 25, 70 };
    static const int neb_r[3] = { 35, 25, 30 };
    uint32_t neb_cols[3];
    neb_cols[0] = th->accent1;
    neb_cols[1] = th->accent2;
    neb_cols[2] = th->accent3;

    for (int n = 0; n < 3; n++) {
        int cx = w * neb_x[n] / 100;
        int cy = h * neb_y[n] / 100;
        int rr = (w < h ? w : h) * neb_r[n] / 100;

        /* Drift slightly */
        cx += icos((int)(t / 2 + n * 85)) * 5 / 127;
        cy += isin((int)(t / 2 + n * 85)) * 5 / 127;

        int xmin = cx - rr; if (xmin < 0) xmin = 0;
        int xmax = cx + rr; if (xmax > w) xmax = w;
        int ymin = cy - rr; if (ymin < 0) ymin = 0;
        int ymax = cy + rr; if (ymax > h) ymax = h;

        int nr = (neb_cols[n] >> 16) & 0xFF;
        int ng = (neb_cols[n] >>  8) & 0xFF;
        int nb = neb_cols[n] & 0xFF;

        for (int py = ymin; py < ymax; py++) {
            for (int px = xmin; px < xmax; px++) {
                int dx = px - cx, dy = py - cy;
                int d2 = dx*dx + dy*dy;
                int r2 = rr * rr;
                if (d2 >= r2) continue;
                int alpha = 45 - 45 * d2 / r2;
                uint32_t *p = &buf[py * w + px];
                int dr = (*p >> 16) & 0xFF;
                int dg = (*p >>  8) & 0xFF;
                int db =  *p        & 0xFF;
                buf[py * w + px] = mkrgb(
                    dr + (nr - dr) * alpha / 255,
                    dg + (ng - dg) * alpha / 255,
                    db + (nb - db) * alpha / 255);
            }
        }
    }

    /* 200 deterministic stars with flicker */
    static const uint32_t star_seeds[3] = { 42, 77, 123 };
    uint32_t seed = star_seeds[theme_idx];
    for (int i = 0; i < 200; i++) {
        seed = lcg(seed);
        int sx = (int)(seed % (uint32_t)w);
        seed = lcg(seed);
        int sy = (int)(seed % (uint32_t)h);
        seed = lcg(seed);
        int base_b = 120 + (int)(seed % 136);
        seed = lcg(seed);
        int phase = (int)(seed % 256);

        int flick = isin((int)(t * 2 + phase)) * 40 / 127;
        int br = clamp(base_b + flick, 0, 255);

        /* Slightly tinted */
        int tint = (int)(seed % 3);
        uint32_t sc = (tint == 0) ? mkrgb(br, br, br + 30) :
                      (tint == 1) ? mkrgb(br + 20, br, br)  :
                                    mkrgb(br, br + 10, br);
        buf[sy * w + sx] = sc;
        /* Brighter stars are 2×2 */
        if (base_b > 200 && sx + 1 < w && sy + 1 < h) {
            buf[sy * w + sx + 1] = sc;
            buf[(sy+1) * w + sx] = sc;
        }
    }
    (void)style;
}

/* ── Style: Waves ───────────────────────────────────────────────── */
/* Matches mockup drawWaves(): sky gradient + 3 wave layers using
   exact color tables from the HTML theme registry.                    */

static void draw_waves(uint32_t *buf, int w, int h, uint32_t t,
                        int style, int theme_idx) {
    const theme_t *th = &wavest[theme_idx];

    /* Full-height sky gradient */
    for (int y = 0; y < h; y++) {
        uint32_t col = vgrad(y, h, th->sky, th->sky_stops);
        for (int x = 0; x < w; x++)
            buf[y * w + x] = col;
    }

    /* 3 wave layers matching mockup wave colors:
       theme accent1/accent2/accent3 = wave layer 0/1/2 */
    uint32_t wcolors[3] = { th->accent1, th->accent2, th->accent3 };

    for (int lyr = 0; lyr < 3; lyr++) {
        uint32_t wc = wcolors[lyr];
        int wr = (wc >> 16) & 0xFF;
        int wg = (wc >>  8) & 0xFF;
        int wb =  wc        & 0xFF;

        /* baseY = h*0.4 + lyr*(h*0.12) */
        int base_y = h * 40 / 100 + lyr * h * 12 / 100;
        /* amp = h * 0.04 * (1 + lyr*0.3) */
        int amp = h * (10 + lyr * 3) / 250;

        /* Animation phases advance with time, speed proportional to layer */
        int spd = (lyr + 1);
        int t_fwd = (int)((t >> 3) * spd);   /* forward drift */
        int t_bwd = (int)((t >> 4) * spd);   /* backward drift */
        int t_slow = (int)((t >> 5) * spd);  /* slow component */

        for (int x = 0; x < w; x++) {
            /* sin(nx*freq*PI) → isin_phase = x*freq*128/w */
            int p1 = (x * 4 * 128 / w + t_fwd) & 255;
            int p2 = (x * 7 * 128 / w - t_bwd) & 255;
            int p3 = (x * 2 * 128 / w + t_slow + lyr * 40) & 255;

            int wave_off = isin(p1) * amp / 127
                         + isin(p2) * amp * 4 / (127 * 10)
                         + isin(p3) * amp * 3 / (127 * 10);
            int top_y = base_y + wave_off;
            if (top_y < 0) top_y = 0;
            if (top_y >= h) continue;

            for (int y = top_y; y < h; y++) {
                uint32_t *p = &buf[y * w + x];
                int dr = (*p >> 16) & 0xFF;
                int dg = (*p >>  8) & 0xFF;
                int db =  *p        & 0xFF;
                /* Blend wave color at 85% opacity */
                buf[y * w + x] = mkrgb(dr + (wr - dr) * 85 / 100,
                                        dg + (wg - dg) * 85 / 100,
                                        db + (wb - db) * 85 / 100);
            }
        }
    }
    (void)style;
}

/* ── Dispatch table ─────────────────────────────────────────────── */

typedef void (*draw_fn_t)(uint32_t *, int, int, uint32_t, int, int);
static const draw_fn_t draw_fns[WALLPAPER_STYLE_COUNT] = {
    draw_mountains,
    draw_gradient,
    draw_geometric,
    draw_stars_wp,
    draw_waves,
};

static const char *style_names[WALLPAPER_STYLE_COUNT] = {
    "Mountains", "Gradient", "Geometric", "Stars", "Waves"
};

static const int theme_counts[WALLPAPER_STYLE_COUNT] = { 4, 4, 3, 3, 3 };

static const theme_t *all_themes[WALLPAPER_STYLE_COUNT] = {
    mtns, grads, geos, stars_t, wavest
};

/* ── Public API ─────────────────────────────────────────────────── */

void wallpaper_init(void) {
    cur_style = WALLPAPER_MOUNTAINS;
    cur_theme = 0;
}

void wallpaper_draw(uint32_t *buf, int w, int h, uint32_t t) {
    int s = cur_style;
    int th = cur_theme;
    if (s < 0 || s >= WALLPAPER_STYLE_COUNT) s = 0;
    int tc = theme_counts[s];
    if (th < 0 || th >= tc) th = 0;
    draw_fns[s](buf, w, h, t, s, th);
}

void wallpaper_draw_thumbnail(uint32_t *buf, int w, int h,
                               int style_idx, int theme_idx) {
    if (style_idx < 0 || style_idx >= WALLPAPER_STYLE_COUNT) return;
    int tc = theme_counts[style_idx];
    if (theme_idx < 0 || theme_idx >= tc) theme_idx = 0;
    /* Draw at t=64 for a nice mid-animation frame */
    draw_fns[style_idx](buf, w, h, 64, style_idx, theme_idx);
}

void wallpaper_set_style(int style_idx, int theme_idx) {
    if (style_idx < 0 || style_idx >= WALLPAPER_STYLE_COUNT) return;
    cur_style = style_idx;
    int tc = theme_counts[style_idx];
    cur_theme = (theme_idx >= 0 && theme_idx < tc) ? theme_idx : 0;
}

void wallpaper_set_theme(int theme_idx) {
    int tc = theme_counts[cur_style];
    if (theme_idx >= 0 && theme_idx < tc)
        cur_theme = theme_idx;
}

int wallpaper_get_style(void) { return cur_style; }
int wallpaper_get_theme(void) { return cur_theme; }

int wallpaper_theme_count(int style_idx) {
    if (style_idx < 0 || style_idx >= WALLPAPER_STYLE_COUNT) return 1;
    return theme_counts[style_idx];
}

const char *wallpaper_style_name(int style_idx) {
    if (style_idx < 0 || style_idx >= WALLPAPER_STYLE_COUNT) return "";
    return style_names[style_idx];
}

const char *wallpaper_theme_name(int style_idx, int theme_idx) {
    if (style_idx < 0 || style_idx >= WALLPAPER_STYLE_COUNT) return "";
    int tc = theme_counts[style_idx];
    if (theme_idx < 0 || theme_idx >= tc) return "";
    return all_themes[style_idx][theme_idx].name;
}

uint32_t wallpaper_theme_color(int style_idx, int theme_idx) {
    if (style_idx < 0 || style_idx >= WALLPAPER_STYLE_COUNT) return 0xFF808080;
    int tc = theme_counts[style_idx];
    if (theme_idx < 0 || theme_idx >= tc) return 0xFF808080;
    return all_themes[style_idx][theme_idx].dot_color;
}
