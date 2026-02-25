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

typedef struct {
    const char *name;
    uint32_t    dot_color;    /* representative dot for Settings UI */
    uint32_t    sky[4];       /* sky gradient stops */
    uint32_t    accent1;
    uint32_t    accent2;
    uint32_t    accent3;
} theme_t;

/* Mountains: 4 themes */
static const theme_t mtns[4] = {
    { "Night",
      0xFF1A0A2E,
      { 0xFF0A0015, 0xFF1A0A2E, 0xFF2D1B4E, 0xFF3D2060 },
      0xFF7B2FBE, 0xFF3D2060, 0xFF0D0D1A },
    { "Dawn",
      0xFFF5A623,
      { 0xFF1A0A2E, 0xFF5C2E7A, 0xFFB05090, 0xFFF5A623 },
      0xFFB05090, 0xFF5C2E7A, 0xFF1A0020 },
    { "Day",
      0xFF4FC3F7,
      { 0xFF1565C0, 0xFF1E88E5, 0xFF42A5F5, 0xFF81D4FA },
      0xFF0D47A1, 0xFF1565C0, 0xFF1A237E },
    { "Dusk",
      0xFFFF5722,
      { 0xFF1A0A2E, 0xFF6A1545, 0xFFD84315, 0xFFFF8F00 },
      0xFFBF360C, 0xFF6A1545, 0xFF110020 },
};

/* Gradient: 4 themes */
static const theme_t grads[4] = {
    { "Sunset",
      0xFFE91E63,
      { 0xFF4A148C, 0xFFB71C1C, 0xFFE65100, 0xFFF9A825 },
      0xFFE91E63, 0xFFFF5722, 0xFF4A148C },
    { "Ocean",
      0xFF1976D2,
      { 0xFF0D47A1, 0xFF1565C0, 0xFF0277BD, 0xFF00838F },
      0xFF1976D2, 0xFF00838F, 0xFF006064 },
    { "Aurora",
      0xFF00BCD4,
      { 0xFF0A0A1E, 0xFF003040, 0xFF006040, 0xFF00BCD4 },
      0xFF00BCD4, 0xFF00E5FF, 0xFF1DE9B6 },
    { "Midnight",
      0xFF673AB7,
      { 0xFF0A0014, 0xFF1A0033, 0xFF2D006A, 0xFF673AB7 },
      0xFF7B1FA2, 0xFF9C27B0, 0xFF4A148C },
};

/* Geometric: 3 themes */
static const theme_t geos[3] = {
    { "Dark",
      0xFF37474F,
      { 0xFF0D1117, 0xFF161B22, 0xFF21262D, 0xFF30363D },
      0xFF1F6FEB, 0xFF388BFD, 0xFF0D419D },
    { "Colorful",
      0xFFAB47BC,
      { 0xFF1A0533, 0xFF0D2B45, 0xFF0F3460, 0xFF1A1A2E },
      0xFFAB47BC, 0xFFE91E63, 0xFF1565C0 },
    { "Neon",
      0xFFFF00FF,
      { 0xFF030303, 0xFF050505, 0xFF080808, 0xFF0A0A0A },
      0xFFFF00FF, 0xFF00FFFF, 0xFF39FF14 },
};

/* Stars: 3 themes */
static const theme_t stars[3] = {
    { "Deep Space",
      0xFF0D1B2A,
      { 0xFF010207, 0xFF020410, 0xFF040818, 0xFF060C22 },
      0xFF4A235A, 0xFF1B2A4A, 0xFF0D0820 },
    { "Nebula",
      0xFF7B1FA2,
      { 0xFF050010, 0xFF0D001E, 0xFF140030, 0xFF1A0040 },
      0xFF7B1FA2, 0xFF1565C0, 0xFFB71C1C },
    { "Starfield",
      0xFF90A4AE,
      { 0xFF000000, 0xFF010101, 0xFF020204, 0xFF040408 },
      0xFF37474F, 0xFF455A64, 0xFF263238 },
};

/* Waves: 3 themes */
static const theme_t waves[3] = {
    { "Ocean",
      0xFF0277BD,
      { 0xFF80DEEA, 0xFF4FC3F7, 0xFF039BE5, 0xFF0277BD },
      0xFF01579B, 0xFF006064, 0xFF004D40 },
    { "Sunset Sea",
      0xFFE57373,
      { 0xFFFFF176, 0xFFFFB74D, 0xFFEF5350, 0xFFB71C1C },
      0xFF880E4F, 0xFF4A148C, 0xFF1A0030 },
    { "Arctic",
      0xFFB0BEC5,
      { 0xFFCFD8DC, 0xFFB0BEC5, 0xFF90A4AE, 0xFF78909C },
      0xFF546E7A, 0xFF37474F, 0xFF263238 },
};

/* ── State ──────────────────────────────────────────────────────── */

static int  cur_style = WALLPAPER_MOUNTAINS;
static int  cur_theme = 0;

/* ── Style: Mountains ───────────────────────────────────────────── */

static void draw_mountains(uint32_t *buf, int w, int h, uint32_t t,
                            int style, int theme_idx) {
    const theme_t *th = &mtns[theme_idx];

    /* Sky gradient */
    for (int y = 0; y < h; y++) {
        uint32_t col = vgrad(y, h, th->sky, 4);
        for (int x = 0; x < w; x++)
            buf[y * w + x] = col;
    }

    /* Stars (night/dawn themes) */
    if (theme_idx == 0 || theme_idx == 1) {
        uint32_t seed = 42;
        int star_count = 200;
        int brightness = (theme_idx == 0) ? 200 : 80;
        for (int i = 0; i < star_count; i++) {
            seed = lcg(seed);
            int sx = (int)(seed % (uint32_t)w);
            seed = lcg(seed);
            int sy = (int)(seed % (uint32_t)(h * 2 / 3));
            seed = lcg(seed);
            int bright = brightness - (int)(seed % 60);
            /* Flicker with trig */
            int flick = isin((int)(t * 2 + i * 13)) * 30 / 127;
            bright += flick;
            bright = clamp(bright, 0, 255);
            buf[sy * w + sx] = mkrgb(bright, bright, bright + 20);
        }
    }

    /* Aurora (night theme) */
    if (theme_idx == 0) {
        int aurora_y = h / 4;
        int aurora_h = h / 8;
        for (int y = aurora_y; y < aurora_y + aurora_h; y++) {
            int alpha = 80 - (y - aurora_y) * 80 / aurora_h;
            for (int x = 0; x < w; x++) {
                int wave = isin((int)(x * 256 / w * 3 + t * 4)) * aurora_h / 256;
                int ay = y + wave;
                if (ay < 0 || ay >= h) continue;
                uint32_t *p = &buf[ay * w + x];
                int dr = (*p >> 16) & 0xFF;
                int dg = (*p >>  8) & 0xFF;
                int db =  *p        & 0xFF;
                int ar = (th->accent1 >> 16) & 0xFF;
                int ag = (th->accent1 >>  8) & 0xFF;
                int ab =  th->accent1        & 0xFF;
                buf[ay * w + x] = mkrgb(
                    dr + (ar - dr) * alpha / 255,
                    dg + (ag - dg) * alpha / 255,
                    db + (ab - db) * alpha / 255);
            }
        }
    }

    /* Three mountain layers (far to near) */
    static const int peak_seeds[3] = { 17, 37, 53 };
    static const int base_frac[3]  = { 55, 68, 80 };  /* % of height */
    static const int peak_frac[3]  = { 30, 45, 62 };  /* % of height */
    static const int dark_pct[3]   = { 60, 40, 25 };  /* % brightness */

    for (int layer = 0; layer < 3; layer++) {
        int base_y = h * base_frac[layer] / 100;
        int peak_y = h * peak_frac[layer] / 100;
        uint32_t mcolor = lerp_c(th->accent2, th->accent3,
                                 layer, 3);
        /* Darken for depth */
        int dr = (int)((mcolor >> 16) & 0xFF) * dark_pct[layer] / 100;
        int dg = (int)((mcolor >>  8) & 0xFF) * dark_pct[layer] / 100;
        int db = (int)( mcolor        & 0xFF) * dark_pct[layer] / 100;
        mcolor = mkrgb(dr, dg, db);

        /* Generate mountain silhouette via LCG-based heightmap */
        uint32_t mseed = (uint32_t)peak_seeds[layer];
        int prev_h = base_y;
        int segs = 16;
        for (int seg = 0; seg <= segs; seg++) {
            mseed = lcg(mseed);
            int sx = seg * w / segs;
            int target_h;
            if (seg == 0 || seg == segs)
                target_h = base_y;
            else
                target_h = peak_y + (int)(mseed % (uint32_t)(base_y - peak_y));

            int next_sx = (seg + 1) * w / segs;
            int next_mseed = lcg(mseed);
            int next_h;
            if (seg + 1 == 0 || seg + 1 >= segs)
                next_h = base_y;
            else
                next_h = peak_y + (int)(next_mseed % (uint32_t)(base_y - peak_y));

            /* Fill trapezoid from sx to next_sx */
            for (int x = sx; x < next_sx && x < w; x++) {
                int frac = (next_sx > sx) ? (x - sx) * 256 / (next_sx - sx) : 128;
                int top_y = target_h + (next_h - target_h) * frac / 256;
                if (seg > 0) top_y = prev_h + (top_y - prev_h) * frac / 256;
                for (int y = top_y; y < base_y && y >= 0 && y < h; y++)
                    buf[y * w + x] = mcolor;
            }
            prev_h = target_h;
        }
    }

    (void)style;
}

/* ── Style: Gradient ────────────────────────────────────────────── */

static void draw_gradient(uint32_t *buf, int w, int h, uint32_t t,
                           int style, int theme_idx) {
    const theme_t *th = &grads[theme_idx];

    /* Slowly drifting diagonal gradient */
    int angle = (int)(t * 1) & 511; /* 0..511 = 0..2*PI*2 */
    int ax = icos(angle / 2) * 100 / 127; /* -100 to 100 */
    int ay = isin(angle / 2) * 100 / 127;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            /* Project (x,y) onto gradient direction */
            int proj = (x * (ax + 100) / w + y * (ay + 100) / h);
            proj = clamp(proj, 0, 199);
            uint32_t col = vgrad(proj, 200, th->sky, 4);
            buf[y * w + x] = col;
        }
    }

    /* 3 soft orbs */
    static const int orb_x[3] = { 25, 65, 80 }; /* % of width */
    static const int orb_y[3] = { 30, 70, 20 };
    static const int orb_r[3] = { 30, 25, 20 };  /* % of min(w,h) */
    uint32_t orb_cols[3];
    orb_cols[0] = th->accent1;
    orb_cols[1] = th->accent2;
    orb_cols[2] = th->accent3;

    for (int o = 0; o < 3; o++) {
        /* Orbs drift in small circles */
        int drift = 5;
        int cx = w * orb_x[o] / 100 + icos((int)(t * 2 + o * 85)) * drift / 127;
        int cy = h * orb_y[o] / 100 + isin((int)(t * 2 + o * 85)) * drift / 127;
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
                int d2 = dx*dx + dy*dy;
                int r2 = radius * radius;
                if (d2 >= r2) continue;
                /* Gaussian-ish falloff */
                int alpha = 60 - 60 * d2 / r2;
                uint32_t *p = &buf[py * w + px];
                int dr = (*p >> 16) & 0xFF;
                int dg = (*p >>  8) & 0xFF;
                int db =  *p        & 0xFF;
                buf[py * w + px] = mkrgb(
                    dr + (or_ - dr) * alpha / 255,
                    dg + (og  - dg) * alpha / 255,
                    db + (ob  - db) * alpha / 255);
            }
        }
    }
    (void)style;
}

/* ── Style: Geometric ───────────────────────────────────────────── */

static void draw_geometric(uint32_t *buf, int w, int h, uint32_t t,
                            int style, int theme_idx) {
    const theme_t *th = &geos[theme_idx];

    /* Base fill */
    for (int i = 0; i < w * h; i++) buf[i] = th->sky[0];

    /* Tessellated triangles: grid of cells, each split diagonally */
    int cell = (w < h ? w : h) / 10;
    if (cell < 20) cell = 20;
    int cols = w / cell + 2;
    int rows = h / cell + 2;

    uint32_t tri_cols[6];
    tri_cols[0] = th->sky[1];
    tri_cols[1] = th->sky[2];
    tri_cols[2] = th->sky[3];
    tri_cols[3] = th->accent1;
    tri_cols[4] = th->accent2;
    tri_cols[5] = th->accent3;

    uint32_t seed = 1337;
    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            seed = lcg(seed);
            int ci = (int)(seed % 6);
            seed = lcg(seed);
            int dark = (int)(seed % 40);

            /* Pulsing opacity */
            int pulse = isin((int)(t + row * 13 + col * 17)) * 20 / 127;

            int x0 = col * cell, y0 = row * cell;
            int x1 = x0 + cell, y1 = y0 + cell;

            uint32_t col0 = tri_cols[ci];
            int rr = clamp((int)((col0 >> 16) & 0xFF) - dark + pulse, 0, 255);
            int gg = clamp((int)((col0 >>  8) & 0xFF) - dark + pulse, 0, 255);
            int bb = clamp((int)( col0        & 0xFF) - dark + pulse, 0, 255);
            uint32_t fc = mkrgb(rr, gg, bb);

            seed = lcg(seed);
            int ci2 = (int)(seed % 6);
            uint32_t col1 = tri_cols[ci2];
            rr = clamp((int)((col1 >> 16) & 0xFF) - dark - pulse, 0, 255);
            gg = clamp((int)((col1 >>  8) & 0xFF) - dark - pulse, 0, 255);
            bb = clamp((int)( col1        & 0xFF) - dark - pulse, 0, 255);
            uint32_t sc = mkrgb(rr, gg, bb);

            /* Upper-left triangle (above diagonal) */
            for (int py = y0; py < y1 && py < h; py++) {
                for (int px = x0; px < x1 && px < w; px++) {
                    if (px < 0 || py < 0) continue;
                    /* Is point above diagonal? */
                    int above = (px - x0) + (py - y0) < cell;
                    buf[py * w + px] = above ? fc : sc;
                }
            }
        }
    }
    (void)style;
}

/* ── Style: Stars ───────────────────────────────────────────────── */

static void draw_stars_wp(uint32_t *buf, int w, int h, uint32_t t,
                           int style, int theme_idx) {
    const theme_t *th = &stars[theme_idx];

    /* Deep background */
    for (int y = 0; y < h; y++) {
        uint32_t col = vgrad(y, h, th->sky, 4);
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

static void draw_waves(uint32_t *buf, int w, int h, uint32_t t,
                        int style, int theme_idx) {
    const theme_t *th = &waves[theme_idx];

    /* Sky gradient (top 55%) */
    int sky_h = h * 55 / 100;
    for (int y = 0; y < sky_h; y++) {
        uint32_t col = vgrad(y, sky_h, th->sky, 4);
        for (int x = 0; x < w; x++)
            buf[y * w + x] = col;
    }

    /* Wave layers (bottom 60%, overlapping) */
    static const int wave_speed[5] = { 3, 5, 2, 7, 4 };
    static const int wave_amp[5]   = { 6, 4, 8, 3, 5 };
    static const int wave_freq[5]  = { 3, 5, 2, 7, 4 };
    static const int wave_y_pct[5] = { 52, 58, 62, 67, 73 };

    for (int lyr = 0; lyr < 5; lyr++) {
        int base_y = h * wave_y_pct[lyr] / 100;
        int amp = h * wave_amp[lyr] / 100;

        /* Wave color darkens toward bottom */
        uint32_t wc = lerp_c(th->accent1, th->accent3, lyr, 5);

        for (int x = 0; x < w; x++) {
            int phase1 = x * wave_freq[lyr] * 256 / w + (int)(t * wave_speed[lyr]);
            int phase2 = x * (wave_freq[lyr] + 1) * 256 / w - (int)(t * wave_speed[lyr] / 2);
            int wave_off = isin(phase1) * amp / 127 +
                           isin(phase2) * amp / 2 / 127;
            int top_y = base_y + wave_off;

            for (int y = top_y; y < h; y++) {
                if (y < 0 || y >= h) continue;
                /* Darken toward bottom */
                int depth_dark = (y - top_y) * 30 / (h - top_y + 1);
                int wr = clamp((int)((wc >> 16) & 0xFF) - depth_dark, 0, 255);
                int wg = clamp((int)((wc >>  8) & 0xFF) - depth_dark, 0, 255);
                int wb = clamp((int)( wc        & 0xFF) - depth_dark, 0, 255);
                buf[y * w + x] = mkrgb(wr, wg, wb);
            }

            /* White foam line at wave top */
            int foam_y = clamp(top_y, 0, h - 1);
            int foam_a = 150 - lyr * 25;
            if (foam_a > 0) {
                uint32_t *p = &buf[foam_y * w + x];
                int dr = (*p >> 16) & 0xFF;
                int dg = (*p >>  8) & 0xFF;
                int db =  *p        & 0xFF;
                buf[foam_y * w + x] = mkrgb(
                    dr + (255 - dr) * foam_a / 255,
                    dg + (255 - dg) * foam_a / 255,
                    db + (255 - db) * foam_a / 255);
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
    mtns, grads, geos, stars, waves
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
