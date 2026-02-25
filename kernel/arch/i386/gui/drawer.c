/* drawer.c — App drawer: full-screen overlay with search + category grid.
 *
 * Opened by Tab key or radial center click.
 * Shows all apps organized by category with live search filtering.
 * Right-click tile → toggle pin.
 */
#include <kernel/drawer.h>
#include <kernel/compositor.h>
#include <kernel/app.h>
#include <kernel/icon_cache.h>
#include <kernel/gfx.h>
#include <string.h>

/* ── Layout constants ───────────────────────────────────────────── */
#define TILE_SIZE   80
#define TILE_GAP     8
#define TILE_ICON   46
#define TILE_COLS   8
#define SEARCH_H    44
#define CAT_H       22
#define HEADER_PAD  20   /* top padding */
#define SIDE_PAD    60
#define MAX_VISIBLE 64   /* max tiles shown at once */

/* ── State ──────────────────────────────────────────────────────── */
static comp_surface_t *surf = 0;
static int vis = 0;

static char search[64];
static int  search_len = 0;

/* Filtered list */
static int filtered[APP_MAX];
static int filtered_count = 0;

static int hover_tile = -1;  /* tile under mouse */

/* ── Search/filter ──────────────────────────────────────────────── */

static int str_has_prefix(const char *hay, const char *needle, int igncase) {
    while (*needle) {
        char h = *hay, n = *needle;
        if (igncase) {
            if (h >= 'A' && h <= 'Z') h += 32;
            if (n >= 'A' && n <= 'Z') n += 32;
        }
        if (h != n) return 0;
        hay++; needle++;
    }
    return 1;
}

static int str_contains(const char *hay, const char *needle, int igncase) {
    while (*hay) {
        if (str_has_prefix(hay, needle, igncase)) return 1;
        hay++;
    }
    return 0;
}

static int score_app(const app_info_t *ai, const char *q) {
    if (!q || !q[0]) return 1; /* no filter = include all */
    if (str_has_prefix(ai->name, q, 1)) return 100;
    if (str_contains(ai->name, q, 1)) return 60;
    if (str_has_prefix(ai->id, q, 1)) return 50;
    if (str_contains(ai->id, q, 1)) return 40;
    if (str_contains(app_cat_name(ai->category), q, 1)) return 20;
    return 0;
}

static void rebuild_filter(void) {
    filtered_count = 0;
    int n = app_get_count();
    for (int i = 0; i < n && filtered_count < MAX_VISIBLE; i++) {
        const app_info_t *ai = app_get(i);
        if (!ai) continue;
        if (score_app(ai, search) > 0)
            filtered[filtered_count++] = i;
    }
    hover_tile = -1;
}

/* ── Drawing helpers ────────────────────────────────────────────── */

static void fill_rect(uint32_t *px, int pw, int x, int y,
                       int w, int h, uint32_t color) {
    for (int row = y; row < y + h && row < (int)surf->h; row++) {
        if (row < 0) continue;
        for (int col = x; col < x + w && col < (int)surf->w; col++) {
            if (col < 0) continue;
            px[row * pw + col] = color;
        }
    }
}

static void draw_rrect(uint32_t *px, int pw, int x, int y,
                        int w, int h, int r, uint32_t color) {
    for (int row = y; row < y + h; row++) {
        if (row < 0 || row >= (int)surf->h) continue;
        for (int col = x; col < x + w; col++) {
            if (col < 0 || col >= (int)surf->w) continue;
            int dx = 0, dy = 0, inside = 1;
            if (col<x+r && row<y+r) { dx=col-(x+r); dy=row-(y+r); }
            else if (col>=x+w-r && row<y+r) { dx=col-(x+w-r-1); dy=row-(y+r); }
            else if (col<x+r && row>=y+h-r) { dx=col-(x+r); dy=row-(y+h-r-1); }
            else if (col>=x+w-r && row>=y+h-r) { dx=col-(x+w-r-1); dy=row-(y+h-r-1); }
            if (dx||dy) inside=(dx*dx+dy*dy<=r*r);
            if (inside) px[row*pw+col] = color;
        }
    }
}

/* ── Tile geometry ──────────────────────────────────────────────── */

static int layout_x0;   /* left edge of tile grid */
static int layout_y0;   /* top of first tile row */
static int layout_cols;

static void calc_layout(void) {
    int sw = surf->w;
    layout_cols = (sw - 2 * SIDE_PAD + TILE_GAP) / (TILE_SIZE + TILE_GAP);
    if (layout_cols < 1) layout_cols = 1;
    if (layout_cols > TILE_COLS) layout_cols = TILE_COLS;
    int grid_w = layout_cols * (TILE_SIZE + TILE_GAP) - TILE_GAP;
    layout_x0 = (sw - grid_w) / 2;
    layout_y0 = HEADER_PAD + SEARCH_H + 16;
}

static void tile_rect(int tidx, int *ox, int *oy) {
    int col = tidx % layout_cols;
    int row = tidx / layout_cols;
    *ox = layout_x0 + col * (TILE_SIZE + TILE_GAP);
    *oy = layout_y0 + row * (TILE_SIZE + TILE_GAP);
}

/* ── Full repaint ───────────────────────────────────────────────── */

void drawer_paint(void) {
    if (!surf || !vis) return;

    int sw = surf->w, sh = surf->h;
    uint32_t *px = surf->pixels;

    /* Background */
    for (int i = 0; i < sw * sh; i++) px[i] = 0xB8000000; /* ~72% black */

    calc_layout();

    gfx_surface_t gs;
    gs.buf = px; gs.w = sw; gs.h = sh; gs.pitch = sw;

    /* ── Search bar ────────────────────────────────────── */
    int sb_w = sw - 2 * SIDE_PAD;
    int sb_x = SIDE_PAD;
    int sb_y = HEADER_PAD;
    draw_rrect(px, sw, sb_x, sb_y, sb_w, SEARCH_H, 11, 0x12FFFFFF);
    /* Border */
    for (int col = sb_x; col < sb_x + sb_w; col++) {
        px[sb_y * sw + col] = 0x28FFFFFF;
        px[(sb_y + SEARCH_H - 1) * sw + col] = 0x28FFFFFF;
    }

    /* Search text or placeholder */
    int text_y = sb_y + (SEARCH_H - 16) / 2;
    if (search_len > 0) {
        gfx_surf_draw_string(&gs, sb_x + 16, text_y, search,
                             0xFFCDD6F4, 0);
        /* Cursor */
        int cursor_x = sb_x + 16 + search_len * 8;
        fill_rect(px, sw, cursor_x, text_y, 2, 16, 0xFFCDD6F4);
    } else {
        gfx_surf_draw_string(&gs, sb_x + 16, text_y,
                             "Search apps...",
                             0xFF45475A, 0);
    }

    /* Match count */
    if (search_len > 0) {
        char count_str[32];
        int n = filtered_count;
        int i = 0;
        /* Build "%d apps" */
        if (n == 0) { count_str[i++] = 'N'; count_str[i++] = 'o'; count_str[i++] = 'n'; count_str[i++] = 'e'; }
        else {
            if (n >= 10) count_str[i++] = '0' + n / 10;
            count_str[i++] = '0' + n % 10;
            count_str[i++] = ' '; count_str[i++] = 'a'; count_str[i++] = 'p';
            count_str[i++] = 'p'; if (n != 1) count_str[i++] = 's';
        }
        count_str[i] = '\0';
        gfx_surf_draw_string(&gs, sb_x + sb_w - i * 8 - 16, text_y,
                             count_str, 0xFF45475A, 0);
    }

    /* ── App tiles ─────────────────────────────────────── */
    int max_tiles = filtered_count;
    for (int ti = 0; ti < max_tiles; ti++) {
        int app_idx = filtered[ti];
        const app_info_t *ai = app_get(app_idx);
        if (!ai) continue;

        int tx, ty;
        tile_rect(ti, &tx, &ty);
        if (ty + TILE_SIZE > sh) break;

        /* Hover highlight */
        if (ti == hover_tile) {
            draw_rrect(px, sw, tx, ty, TILE_SIZE, TILE_SIZE, 8, 0x12FFFFFF);
        }

        /* Icon */
        int ix = tx + (TILE_SIZE - TILE_ICON) / 2;
        int iy = ty;
        uint32_t bg = ai->color;
        icon_draw(ai->icon_id, px, sw, ix, iy, TILE_ICON, bg, 0xFFFFFFFF);

        /* Pin ring */
        if (app_is_pinned(app_idx)) {
            /* Draw blue ring around icon */
            int r = TILE_ICON / 2 + 2;
            int cx_ = ix + TILE_ICON / 2, cy_ = iy + TILE_ICON / 2;
            for (int row = cy_ - r; row <= cy_ + r; row++) {
                if (row < 0 || row >= sh) continue;
                for (int col = cx_ - r; col <= cx_ + r; col++) {
                    if (col < 0 || col >= sw) continue;
                    int dx = col - cx_, dy = row - cy_;
                    int d2 = dx*dx + dy*dy;
                    int ro = r, ri = r - 2;
                    if (d2 >= ri*ri && d2 <= ro*ro)
                        px[row * sw + col] = 0xFF3478F6;
                }
            }
        }

        /* Label */
        int lx = tx;
        int ly = ty + TILE_ICON + 4;
        const char *name = ai->name;
        int nlen = 0;
        const char *p = name;
        while (*p++) nlen++;
        int label_x = tx + (TILE_SIZE - nlen * 8) / 2;
        if (label_x < tx) label_x = tx;
        uint32_t label_color = (ti == hover_tile) ? 0xFFCDD6F4 : 0xFFA6ADC8;
        gfx_surf_draw_string(&gs, label_x, ly, name, label_color, 0);
        (void)lx;
    }

    /* Pin hint at bottom */
    {
        const char *hint = "Right-click to pin";
        int hlen = 18;
        int hx = (sw - hlen * 8) / 2;
        gfx_surf_draw_string(&gs, hx, sh - 24,
                             hint, 0xFF45475A, 0);
        /* Pin count */
        int pc = app_pin_count();
        char pcount[16];
        pcount[0] = '0' + pc;
        pcount[1] = ' '; pcount[2] = '/'; pcount[3] = ' ';
        pcount[4] = '0' + APP_MAX_PINNED;
        pcount[5] = '\0';
        gfx_surf_draw_string(&gs, sw / 2 + 80, sh - 24,
                             pcount, 0xFF6C7086, 0);
    }

    comp_surface_damage_all(surf);
}

/* ── Public API ─────────────────────────────────────────────────── */

void drawer_init(void) {
    int sw = (int)gfx_width(), sh = (int)gfx_height();
    surf = comp_surface_create(sw, sh, COMP_LAYER_OVERLAY);
    if (surf) {
        comp_surface_set_visible(surf, 0);
        comp_surface_raise(surf);
    }
    vis = 0; search[0] = '\0'; search_len = 0;
    rebuild_filter();
}

void drawer_show(const char *prefill) {
    if (!surf) return;
    if (prefill && prefill[0]) {
        int i = 0;
        while (prefill[i] && i < 62) {
            search[i] = prefill[i]; i++;
        }
        search[i] = '\0';
        search_len = i;
    } else {
        search[0] = '\0';
        search_len = 0;
    }
    rebuild_filter();
    vis = 1;
    comp_surface_set_visible(surf, 1);
    comp_surface_raise(surf);
    drawer_paint();
}

void drawer_hide(void) {
    if (!surf) return;
    vis = 0;
    comp_surface_set_visible(surf, 0);
    comp_surface_damage_all(surf);
}

int drawer_visible(void) { return vis; }

int drawer_mouse(int mx, int my, int btn_down, int btn_up, int right_click) {
    if (!vis || !surf) return 0;

    calc_layout();

    /* Find tile under mouse */
    int new_hover = -1;
    for (int ti = 0; ti < filtered_count; ti++) {
        int tx, ty;
        tile_rect(ti, &tx, &ty);
        if (mx >= tx && mx < tx + TILE_SIZE &&
            my >= ty && my < ty + TILE_SIZE) {
            new_hover = ti;
            break;
        }
    }

    int needs_repaint = (new_hover != hover_tile);
    hover_tile = new_hover;

    if (btn_up && !right_click) {
        if (hover_tile >= 0) {
            int app_idx = filtered[hover_tile];
            const app_info_t *ai = app_get(app_idx);
            drawer_hide();
            if (ai) app_launch(ai->id);
            return 1;
        }
        /* Click outside any tile: close drawer */
        drawer_hide();
        return 1;
    }

    if (btn_up && right_click) {
        if (hover_tile >= 0) {
            int app_idx = filtered[hover_tile];
            if (app_is_pinned(app_idx)) {
                app_pin_toggle(app_idx);
            } else if (app_pin_count() < APP_MAX_PINNED) {
                app_pin_toggle(app_idx);
            }
            needs_repaint = 1;
        }
    }

    if (needs_repaint) drawer_paint();
    return 1; /* consume all events while drawer is open */
    (void)btn_down;
}

int drawer_key(char ch, int scancode) {
    if (!vis) return 0;
    (void)scancode;

    if (ch == 27) { /* Escape */
        drawer_hide();
        return 1;
    }
    if (ch == 9) { /* Tab → close */
        drawer_hide();
        return 1;
    }
    if (ch == 8 || ch == 127) { /* Backspace */
        if (search_len > 0) {
            search[--search_len] = '\0';
            rebuild_filter();
            drawer_paint();
        }
        return 1;
    }
    if (ch == 13) { /* Enter → launch first result */
        if (filtered_count > 0) {
            int idx = filtered[hover_tile >= 0 ? hover_tile : 0];
            const app_info_t *ai = app_get(idx);
            drawer_hide();
            if (ai) app_launch(ai->id);
        }
        return 1;
    }
    /* Printable char: append to search */
    if (ch >= 32 && ch < 127 && search_len < 62) {
        search[search_len++] = ch;
        search[search_len] = '\0';
        rebuild_filter();
        drawer_paint();
        return 1;
    }
    return 1; /* consume all keys while drawer open */
}
