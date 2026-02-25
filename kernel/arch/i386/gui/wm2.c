#include <kernel/wm2.h>
#include <kernel/compositor.h>
#include <kernel/anim.h>
#include <kernel/gfx.h>
#include <kernel/menubar.h>
#include <kernel/mouse.h>
#include <string.h>
#include <stdlib.h>

#define WM2_MAX_WINDOWS   32
#define WM2_TITLEBAR_H    38    /* px — title bar height (mockup: 38px) */
#define WM2_RESIZE_ZONE    4    /* px — invisible edge resize handle    */
#define WM2_CORNER_R      12    /* px — corner radius (mockup: 12px)    */
#define WM2_BTN_R          6    /* px — traffic-light button radius     */
#define WM2_BTN_SPACING   19    /* px — button centre-to-centre (7gap+12btn) */
#define WM2_BTN_MARGIN    14    /* px — left edge → first button ctr    */
#define WM2_MIN_W        120
#define WM2_MIN_H         60

/* Traffic-light palette — exact mockup hex: #ff5f57 / #ffbd2e / #28c840 */
#define WM2_BTN_CLOSE_C  GFX_RGB(255, 95,  87)
#define WM2_BTN_MIN_C    GFX_RGB(255, 189, 46)
#define WM2_BTN_MAX_C    GFX_RGB( 40, 200, 64)

/* Window colours — mockup: rgba(18,24,36) body / rgba(255,255,255,0.04) titlebar */
#define WM2_BODY_BG      GFX_RGB(18,  24,  36)   /* #121824 */
#define WM2_TITLE_BG     GFX_RGB(26,  32,  48)   /* #1A2030 — body + 0.04 white */
#define WM2_BORDER_C     0x1AFFFFFF               /* rgba(255,255,255,0.10) */
#define WM2_SEP_C        GFX_RGB(30,  38,  56)   /* titlebar separator */

typedef struct {
    int  id;
    int  x, y, w, h;
    char title[64];
    int  state;               /* WM2_STATE_* */
    int  focused;
    int  z;

    int  save_x, save_y, save_w, save_h;

    comp_surface_t *surf;

    uint32_t *client_px;
    int       client_w;
    int       client_h;

    int btn_hover;            /* 0=none 1=close 2=min 3=max           */
    int btns_hovered;         /* 1 if mouse is over button group area */
    int close_requested;
    int in_use;

    /* Animations */
    int open_alpha, open_anim_id;       /* fade-in on create */
    int close_alpha, close_anim_id;     /* fade-out before destroy */
    int closing;                         /* 1 = close animation in progress */
    int min_alpha, min_anim_id;         /* minimize fade */
    int minimizing;                      /* 1 = minimize animation in progress */
    int rest_alpha, rest_anim_id;       /* restore fade */
    int restoring;                       /* 1 = restore animation in progress */
} wm2_win_t;

static wm2_win_t wins[WM2_MAX_WINDOWS];
static int win_count  = 0;
static int focused_id = -1;
static int z_counter  = 0;
static int next_id    = 1;

#define DRAG_NONE    0
#define DRAG_MOVE    1
#define DRAG_RESIZE  2
#define DRAG_BTN     3

static struct {
    int mode, win_id, hit;
    int start_mx, start_my;
    int start_wx, start_wy, start_ww, start_wh;
} drag;

static wm2_win_t *find_win(int id) {
    int i;
    for (i = 0; i < WM2_MAX_WINDOWS; i++)
        if (wins[i].in_use && wins[i].id == id) return &wins[i];
    return 0;
}

static void content_rect(wm2_win_t *win,
                          int *cx, int *cy, int *cw, int *ch) {
    *cx = win->x + 1;
    *cy = win->y + WM2_TITLEBAR_H;
    *cw = win->w - 2;  if (*cw < 0) *cw = 0;
    *ch = win->h - WM2_TITLEBAR_H - 1;  if (*ch < 0) *ch = 0;
}

static int hit_test_win(wm2_win_t *win, int mx, int my) {
    int x = win->x, y = win->y, w = win->w, h = win->h;
    int rx, ry, b, RZ;

    if (mx < x || mx >= x+w || my < y || my >= y+h) return WM2_HIT_NONE;

    rx = mx - x;  ry = my - y;
    RZ = WM2_RESIZE_ZONE;

    /* Corners first, then edges */
    if (rx < RZ && ry < RZ)          return WM2_HIT_RESIZE_NW;
    if (rx >= w-RZ && ry < RZ)       return WM2_HIT_RESIZE_NE;
    if (rx < RZ && ry >= h-RZ)       return WM2_HIT_RESIZE_SW;
    if (rx >= w-RZ && ry >= h-RZ)    return WM2_HIT_RESIZE_SE;
    if (rx < RZ)                      return WM2_HIT_RESIZE_W;
    if (rx >= w-RZ)                   return WM2_HIT_RESIZE_E;
    if (ry >= h-RZ)                   return WM2_HIT_RESIZE_S;
    /* No top-only resize: title bar covers y<TITLEBAR_H */

    if (ry < WM2_TITLEBAR_H) {
        int by = WM2_TITLEBAR_H / 2;
        for (b = 0; b < 3; b++) {
            int bcx = WM2_BTN_MARGIN + b * WM2_BTN_SPACING;
            int dx = rx - bcx, dy = ry - by;
            if (dx*dx + dy*dy <= WM2_BTN_R * WM2_BTN_R)
                return WM2_HIT_BTN_CLOSE + b;
        }
        return WM2_HIT_TITLEBAR;
    }
    return WM2_HIT_CONTENT;
}

static wm2_win_t *topmost_at(int mx, int my) {
    int i, top_z = -1;
    wm2_win_t *result = 0;
    for (i = 0; i < WM2_MAX_WINDOWS; i++) {
        if (!wins[i].in_use || wins[i].state == WM2_STATE_MINIMIZED) continue;
        if (hit_test_win(&wins[i], mx, my) == WM2_HIT_NONE) continue;
        if (wins[i].z > top_z) { top_z = wins[i].z; result = &wins[i]; }
    }
    return result;
}

/* Zero corner pixels outside radius r to create rounded corners */
static void apply_corner_mask(uint32_t *px, int w, int h, int r) {
    int x, y;
    for (y = 0; y < r; y++) {
        for (x = 0; x < r; x++) {
            int ex = r - x, ey = r - y;
            if (ex*ex + ey*ey > r*r) {
                px[y*w + x]               = 0;
                px[y*w + (w-1-x)]         = 0;
                px[(h-1-y)*w + x]         = 0;
                px[(h-1-y)*w + (w-1-x)]   = 0;
            }
        }
    }
}

/* Forward declaration */
static void draw_btn_symbol(uint32_t *px, int sw, int sh,
                             int bcx, int by, int btn_idx);

/* ── Decoration drawing ─────────────────────────────────────────── */


static void blit_client(wm2_win_t *win) {
    int cx, cy, cw, ch, sx, sy, bw, bh, row;
    if (!win->client_px || !win->surf) return;
    content_rect(win, &cx, &cy, &cw, &ch);
    sx = cx - win->x;
    sy = cy - win->y;
    bw = (win->client_w < cw) ? win->client_w : cw;
    bh = (win->client_h < ch) ? win->client_h : ch;
    for (row = 0; row < bh; row++)
        memcpy(win->surf->pixels + (sy+row)*win->surf->w + sx,
               win->client_px   + row * win->client_w,
               (size_t)bw * 4);
}

static void blit_client_region(wm2_win_t *win, int rx, int ry, int rw, int rh) {
    int cx, cy, cw, ch, sx, sy, bw, bh, row;
    if (!win->client_px || !win->surf) return;
    content_rect(win, &cx, &cy, &cw, &ch);
    sx = cx - win->x;
    sy = cy - win->y;
    if (rx < 0) { rw += rx; rx = 0; }
    if (ry < 0) { rh += ry; ry = 0; }
    if (rx + rw > win->client_w) rw = win->client_w - rx;
    if (ry + rh > win->client_h) rh = win->client_h - ry;
    bw = (rx + rw > cw) ? cw - rx : rw;
    bh = (ry + rh > ch) ? ch - ry : rh;
    if (bw <= 0 || bh <= 0) return;
    for (row = 0; row < bh; row++)
        memcpy(win->surf->pixels + (sy + ry + row) * win->surf->w + (sx + rx),
               win->client_px + (ry + row) * win->client_w + rx,
               (size_t)bw * 4);
}

static void draw_win(wm2_win_t *win) {
    static const uint32_t btn_col[3] = {
        WM2_BTN_CLOSE_C, WM2_BTN_MIN_C, WM2_BTN_MAX_C
    };
    gfx_surface_t gs;
    uint32_t fab, cab;
    int sw, sh, b, tx, ty, tlen;

    if (!win->surf) return;
    sw = win->surf->w;
    sh = win->surf->h;
    gs = comp_surface_lock(win->surf);

    fab = WM2_TITLE_BG | 0xFF000000;
    cab = WM2_BODY_BG  | 0xFF000000;

    /* 1. Fill titlebar with glass-dark colour */
    gfx_surf_fill_rect(&gs, 0, 0, sw, WM2_TITLEBAR_H, fab);

    /* 2. Content area body */
    {
        int csw = sw, csh = sh - WM2_TITLEBAR_H;
        if (csw > 0 && csh > 0)
            gfx_surf_fill_rect(&gs, 0, WM2_TITLEBAR_H, csw, csh, cab);
    }

    /* 3. Subtle 1-px border all around (rgba(255,255,255,0.10)) */
    {
        int x, y;
        uint32_t bc = 0xFF1F2B42;  /* body + ~10% white overlay */
        for (x = 0; x < sw; x++) {
            win->surf->pixels[0      * sw + x] = bc;
            win->surf->pixels[(sh-1) * sw + x] = bc;
        }
        for (y = 1; y < sh - 1; y++) {
            win->surf->pixels[y * sw + 0]      = bc;
            win->surf->pixels[y * sw + (sw-1)] = bc;
        }
    }

    /* 4. Round all four corners */
    apply_corner_mask(win->surf->pixels, sw, sh, WM2_CORNER_R);

    /* 5. Separator line between titlebar and content */
    {
        int x;
        uint32_t sep = WM2_SEP_C | 0xFF000000;
        for (x = 0; x < sw; x++)
            win->surf->pixels[(WM2_TITLEBAR_H - 1) * sw + x] = sep;
    }

    /* 6. Traffic-light buttons — always coloured (matches mockup) */
    {
        int by = WM2_TITLEBAR_H / 2;
        for (b = 0; b < 3; b++) {
            int bcx = WM2_BTN_MARGIN + b * WM2_BTN_SPACING;
            uint32_t col = (win->btn_hover == b + 1)
                           ? btn_col[b]
                           : ((win->focused) ? btn_col[b]
                                             : (btn_col[b] & 0xFFBFBFBF)); /* slight dim */
            gfx_surf_fill_circle(&gs, bcx, by, WM2_BTN_R,
                                 col | 0xFF000000);
            /* Draw symbol when hovering the button group */
            if (win->btns_hovered)
                draw_btn_symbol(win->surf->pixels, sw, sh, bcx, by, b);
        }
    }

    /* 7. Title text — centred, light gray (rgba(255,255,255,0.65)) */
    tlen = 0;
    while (win->title[tlen]) tlen++;
    tx = (sw - tlen * FONT_W) / 2;
    ty = (WM2_TITLEBAR_H - FONT_H) / 2;
    {
        int min_tx = WM2_BTN_MARGIN + 3 * WM2_BTN_SPACING + 8;
        if (tx < min_tx) tx = min_tx;
    }
    gfx_surf_draw_string_smooth(&gs, tx, ty, win->title, 0xFFA6A6A6, 1);

    blit_client(win);

    comp_surface_damage_all(win->surf);
}

/* ── Partial button redraw (hover only) ─────────────────────────── */

static void draw_btn_symbol(uint32_t *px, int sw, int sh,
                             int bcx, int by, int btn_idx) {
    /* Draw symbols: 0=close(×), 1=minimize(−), 2=maximize(⤢) */
    uint32_t sym = 0x99000000;  /* dark semi-transparent */
    int d;
    if (btn_idx == 0) {
        /* × for close: two diagonals */
        for (d = -3; d <= 3; d++) {
            int y1 = by + d, x1 = bcx + d, x2 = bcx - d;
            if (y1 >= 0 && y1 < sh) {
                if (x1 >= 0 && x1 < sw) px[y1 * sw + x1] = sym;
                if (x2 >= 0 && x2 < sw) px[y1 * sw + x2] = sym;
            }
        }
    } else if (btn_idx == 1) {
        /* − for minimize: horizontal line */
        for (d = -3; d <= 3; d++) {
            int x1 = bcx + d;
            if (x1 >= 0 && x1 < sw && by >= 0 && by < sh)
                px[by * sw + x1] = sym;
        }
    } else {
        /* ⤢ for maximize: two small diagonal arrows (top-left, bottom-right) */
        /* Top-left arrow */
        for (d = 0; d <= 3; d++) {
            int y1 = by - d, x1 = bcx - d;
            if (y1 >= 0 && y1 < sh && x1 >= 0 && x1 < sw)
                px[y1 * sw + x1] = sym;
        }
        /* Top-left horizontal arm */
        for (d = -3; d <= -1; d++) {
            int x1 = bcx + d;
            if (by - 3 >= 0 && x1 >= 0 && x1 < sw)
                px[(by - 3) * sw + x1] = sym;
        }
        /* Top-left vertical arm */
        for (d = -3; d <= -1; d++) {
            int y1 = by + d;
            if (y1 >= 0 && y1 < sh && bcx - 3 >= 0 && bcx - 3 < sw)
                px[y1 * sw + (bcx - 3)] = sym;
        }
        /* Bottom-right arrow */
        for (d = 0; d <= 3; d++) {
            int y1 = by + d, x1 = bcx + d;
            if (y1 >= 0 && y1 < sh && x1 >= 0 && x1 < sw)
                px[y1 * sw + x1] = sym;
        }
        /* Bottom-right horizontal arm */
        for (d = 1; d <= 3; d++) {
            int x1 = bcx + d;
            if (by + 3 >= 0 && by + 3 < sh && x1 >= 0 && x1 < sw)
                px[(by + 3) * sw + x1] = sym;
        }
        /* Bottom-right vertical arm */
        for (d = 1; d <= 3; d++) {
            int y1 = by + d;
            if (y1 >= 0 && y1 < sh && bcx + 3 >= 0 && bcx + 3 < sw)
                px[y1 * sw + (bcx + 3)] = sym;
        }
    }
}


static void draw_win_buttons(wm2_win_t *win) {
    static const uint32_t btn_col[3] = { WM2_BTN_CLOSE_C, WM2_BTN_MIN_C, WM2_BTN_MAX_C
    };
    gfx_surface_t gs;
    int b, by;
    uint32_t fab;

    if (!win->surf) return;
    gs = comp_surface_lock(win->surf);

    fab = (WM2_TITLE_BG) | 0xFF000000;
    by  = WM2_TITLEBAR_H / 2;

    int sw = win->surf->w, sh = win->surf->h;

    /* Damage rect covering all 3 buttons */
    int bx0 = WM2_BTN_MARGIN - WM2_BTN_R - 1;
    int bx1 = WM2_BTN_MARGIN + 2 * WM2_BTN_SPACING + WM2_BTN_R + 1;
    int by0 = by - WM2_BTN_R - 1;
    int by1 = by + WM2_BTN_R + 1;
    if (bx0 < 0) bx0 = 0;
    if (by0 < 0) by0 = 0;

    gfx_surf_fill_rect(&gs, bx0, by0, bx1 - bx0, by1 - by0, fab);

    /* Redraw all three buttons — always coloured */
    for (b = 0; b < 3; b++) {
        int bcx = WM2_BTN_MARGIN + b * WM2_BTN_SPACING;
        uint32_t col = (win->btn_hover == b + 1)
                       ? btn_col[b]
                       : ((win->focused) ? btn_col[b]
                                         : (btn_col[b] & 0xFFBFBFBF));
        gfx_surf_fill_circle(&gs, bcx, by, WM2_BTN_R, col | 0xFF000000);

        /* Draw symbol when hovering the button group */
        if (win->btns_hovered)
            draw_btn_symbol(win->surf->pixels, sw, sh, bcx, by, b);
    }

    /* Damage only the button area, not the whole surface */
    comp_surface_damage(win->surf, bx0, by0, bx1 - bx0, by1 - by0);
}

static void alloc_client(wm2_win_t *win) {
    int cx, cy, cw, ch;
    content_rect(win, &cx, &cy, &cw, &ch);
    (void)cx; (void)cy;
    if (win->client_px) { free(win->client_px); win->client_px = 0; }
    win->client_w = (cw > 1) ? cw : 1;
    win->client_h = (ch > 1) ? ch : 1;
    win->client_px = (uint32_t *)malloc(
        (size_t)win->client_w * win->client_h * 4);
    if (win->client_px)
        memset(win->client_px, 0,
               (size_t)win->client_w * win->client_h * 4);
}

void wm2_init(void) {
    memset(wins,  0, sizeof(wins));
    memset(&drag, 0, sizeof(drag));
    win_count  = 0;
    focused_id = -1;
    z_counter  = 0;
    next_id    = 1;
}

int wm2_create(int x, int y, int w, int h, const char *title) {
    wm2_win_t *win = 0;
    int i, n = 0;
    if (win_count >= WM2_MAX_WINDOWS) return -1;
    for (i = 0; i < WM2_MAX_WINDOWS; i++)
        if (!wins[i].in_use) { win = &wins[i]; break; }
    if (!win) return -1;

    memset(win, 0, sizeof(*win));
    win->id    = next_id++;
    win->x     = x;  win->y = y;
    win->w     = (w < WM2_MIN_W) ? WM2_MIN_W : w;
    win->h     = (h < WM2_MIN_H) ? WM2_MIN_H : h;
    win->state = WM2_STATE_NORMAL;
    win->in_use = 1;
    if (title)
        while (title[n] && n < 63) { win->title[n] = title[n]; n++; }
    win->title[n] = '\0';

    win->surf = comp_surface_create(win->w, win->h, COMP_LAYER_WINDOWS);
    if (!win->surf) { win->in_use = 0; return -1; }
    comp_surface_move(win->surf, win->x, win->y);

    /* Open fade-in animation */
    win->open_alpha = 0;
    win->open_anim_id = anim_start(&win->open_alpha, 0, 255, 180, ANIM_EASE_OUT);
    win->close_anim_id = -1;
    win->min_anim_id = -1;
    win->rest_anim_id = -1;
    comp_surface_set_alpha(win->surf, 0);

    alloc_client(win);
    win_count++;
    wm2_focus(win->id);
    return win->id;
}

void wm2_destroy(int id) {
    int i;
    wm2_win_t *win = find_win(id);
    if (!win) return;
    if (win->surf)      { comp_surface_destroy(win->surf); win->surf = 0; }
    if (win->client_px) { free(win->client_px); win->client_px = 0; }
    if (drag.win_id == id) drag.mode = DRAG_NONE;
    win->in_use = 0;
    win_count--;
    if (focused_id == id) {
        focused_id = -1;
        for (i = 0; i < WM2_MAX_WINDOWS; i++)
            if (wins[i].in_use) { wm2_focus(wins[i].id); break; }
    }
}

void wm2_focus(int id) {
    wm2_win_t *old, *win;
    if (focused_id == id) return;
    old = find_win(focused_id);
    win = find_win(id);
    if (old) { old->focused = 0; draw_win(old); }
    focused_id = id;
    if (win)  { win->focused = 1; wm2_raise(id); draw_win(win); }
}

void wm2_raise(int id) {
    wm2_win_t *win = find_win(id);
    if (!win || !win->surf) return;
    win->z = ++z_counter;
    comp_surface_raise(win->surf);
}

void wm2_maximize(int id) {
    wm2_win_t *win = find_win(id);
    if (!win || win->state == WM2_STATE_MAXIMIZED) return;
    win->save_x = win->x; win->save_y = win->y;
    win->save_w = win->w; win->save_h = win->h;
    win->state = WM2_STATE_MAXIMIZED;
    wm2_move(id, 0, MENUBAR_HEIGHT);
    wm2_resize(id, (int)gfx_width(), (int)gfx_height() - MENUBAR_HEIGHT);
}

void wm2_restore(int id) {
    wm2_win_t *win = find_win(id);
    if (!win) return;

    if (win->state == WM2_STATE_MAXIMIZED) {
        win->state = WM2_STATE_NORMAL;
        wm2_resize(id, win->save_w, win->save_h);
        wm2_move(id, win->save_x, win->save_y);
    } else if (win->state == WM2_STATE_MINIMIZED) {
        win->state = WM2_STATE_NORMAL;
        win->restoring = 1;
        win->rest_alpha = 0;
        if (win->rest_anim_id >= 0) anim_cancel(win->rest_anim_id);
        win->rest_anim_id = anim_start(&win->rest_alpha, 0, 255, 250, ANIM_EASE_OUT);
        if (win->surf) {
            comp_surface_set_alpha(win->surf, 0);
            comp_surface_set_visible(win->surf, 1);
        }
        wm2_focus(id);
    }
}

void wm2_minimize(int id) {
    wm2_win_t *win = find_win(id);
    if (!win) return;
    win->state = WM2_STATE_MINIMIZED;
    win->minimizing = 1;
    win->min_alpha = 255;
    if (win->min_anim_id >= 0) anim_cancel(win->min_anim_id);
    win->min_anim_id = anim_start(&win->min_alpha, 255, 0, 200, ANIM_EASE_IN);
    /* Actual hide happens in wm2_tick() when animation completes */
}

void wm2_move(int id, int x, int y) {
    wm2_win_t *win = find_win(id);
    if (!win || !win->surf) return;
    win->x = x; win->y = y;
    comp_surface_move(win->surf, x, y);
    /* surface content is surface-local — no redraw needed */
}

void wm2_resize(int id, int nw, int nh) {
    wm2_win_t *win = find_win(id);
    if (!win) return;
    if (nw < WM2_MIN_W) nw = WM2_MIN_W;
    if (nh < WM2_MIN_H) nh = WM2_MIN_H;
    if (win->w == nw && win->h == nh) return;
    win->w = nw;  win->h = nh;

    if (win->surf) comp_surface_resize(win->surf, nw, nh);

    alloc_client(win);
    draw_win(win);
}

int        wm2_get_focused(void) { return focused_id; }
int        wm2_get_count(void)   { return win_count;  }

wm2_info_t wm2_get_info(int id) {
    int i = 0;
    wm2_info_t info;
    wm2_win_t *win;
    memset(&info, 0, sizeof(info));
    win = find_win(id);
    if (!win) { info.id = -1; return info; }
    info.id = win->id;
    info.x  = win->x;  info.y = win->y;
    info.w  = win->w;  info.h = win->h;
    content_rect(win, &info.cx, &info.cy, &info.cw, &info.ch);
    while (win->title[i] && i < 63) { info.title[i] = win->title[i]; i++; }
    info.title[i] = '\0';
    info.state   = win->state;
    info.focused = win->focused;
    return info;
}

uint32_t *wm2_get_canvas(int id, int *out_w, int *out_h) {
    wm2_win_t *win = find_win(id);
    if (!win) {
        if (out_w) *out_w = 0;
        if (out_h) *out_h = 0;
        return 0;
    }
    if (out_w) *out_w = win->client_w;
    if (out_h) *out_h = win->client_h;
    return win->client_px;
}

void wm2_damage_canvas(int id, int x, int y, int w, int h) {
    wm2_win_t *win = find_win(id);
    if (!win || !win->surf) return;
    blit_client_region(win, x, y, w, h);
    comp_surface_damage(win->surf, 1+x, WM2_TITLEBAR_H+y, w, h);
}

void wm2_damage_canvas_all(int id) {
    wm2_win_t *win = find_win(id);
    if (!win || !win->surf) return;
    blit_client(win);
    comp_surface_damage_all(win->surf);
}

int  wm2_close_requested(int id) {
    wm2_win_t *win = find_win(id);
    return win ? win->close_requested : 0;
}

void wm2_clear_close_request(int id) {
    wm2_win_t *win = find_win(id);
    if (win) win->close_requested = 0;
}

void wm2_redraw(int id) {
    wm2_win_t *win = find_win(id);
    if (win) draw_win(win);
}

void wm2_set_all_visible(int visible) {
    for (int i = 0; i < WM2_MAX_WINDOWS; i++) {
        if (!wins[i].in_use || !wins[i].surf) continue;
        if (wins[i].state == WM2_STATE_MINIMIZED) continue;
        comp_surface_set_visible(wins[i].surf, visible);
    }
}

void wm2_redraw_all(void) {
    int i;
    for (i = 0; i < WM2_MAX_WINDOWS; i++)
        if (wins[i].in_use) draw_win(&wins[i]);
}

void wm2_mouse_event(int mx, int my, uint8_t buttons, uint8_t prev_btn) {
    int btn_down = (int)((buttons & (uint8_t)~prev_btn) & MOUSE_BTN_LEFT);
    int btn_up   = (int)(((uint8_t)~buttons & prev_btn) & MOUSE_BTN_LEFT);
    int btn_held = (buttons & MOUSE_BTN_LEFT) ? 1 : 0;
    wm2_win_t *win;

    if (drag.mode == DRAG_MOVE) {
        if (btn_held) {
            int nx = drag.start_wx + (mx - drag.start_mx);
            int ny = drag.start_wy + (my - drag.start_my);
            int sw = (int)gfx_width();
            win = find_win(drag.win_id);
            if (win) {
                if (nx < 0) nx = 0;
                if (ny < 0) ny = 0;
                if (nx + win->w > sw) nx = sw - win->w;
                wm2_move(drag.win_id, nx, ny);
            }
        } else {
            drag.mode = DRAG_NONE;
        }
        return;
    }

    if (drag.mode == DRAG_RESIZE) {
        if (btn_held) {
            int dx  = mx - drag.start_mx;
            int dy  = my - drag.start_my;
            int nx  = drag.start_wx, ny  = drag.start_wy;
            int nw  = drag.start_ww, nh  = drag.start_wh;
            int hit = drag.hit;
            win = find_win(drag.win_id);
            if (!win) { drag.mode = DRAG_NONE; return; }

            if (hit==WM2_HIT_RESIZE_E||hit==WM2_HIT_RESIZE_NE||hit==WM2_HIT_RESIZE_SE) nw += dx;
            if (hit==WM2_HIT_RESIZE_W||hit==WM2_HIT_RESIZE_NW||hit==WM2_HIT_RESIZE_SW) { nx += dx; nw -= dx; }
            if (hit==WM2_HIT_RESIZE_S||hit==WM2_HIT_RESIZE_SE||hit==WM2_HIT_RESIZE_SW) nh += dy;
            if (hit==WM2_HIT_RESIZE_N||hit==WM2_HIT_RESIZE_NW||hit==WM2_HIT_RESIZE_NE) { ny += dy; nh -= dy; }

            if (nw < WM2_MIN_W) {
                if (nx != drag.start_wx) nx = drag.start_wx + drag.start_ww - WM2_MIN_W;
                nw = WM2_MIN_W;
            }
            if (nh < WM2_MIN_H) {
                if (ny != drag.start_wy) ny = drag.start_wy + drag.start_wh - WM2_MIN_H;
                nh = WM2_MIN_H;
            }
            if (nx != win->x || ny != win->y) wm2_move(drag.win_id, nx, ny);
            if (nw != win->w || nh != win->h) wm2_resize(drag.win_id, nw, nh);
        } else {
            drag.mode = DRAG_NONE;
        }
        return;
    }

    if (drag.mode == DRAG_BTN) {
        if (btn_up || !btn_held) {
            win = find_win(drag.win_id);
            if (win && btn_up) {
                if (hit_test_win(win, mx, my) == drag.hit) {
                    if      (drag.hit == WM2_HIT_BTN_CLOSE) win->close_requested = 1;
                    else if (drag.hit == WM2_HIT_BTN_MIN)   wm2_minimize(drag.win_id);
                    else if (drag.hit == WM2_HIT_BTN_MAX) {
                        if (win->state == WM2_STATE_MAXIMIZED)
                            wm2_restore(drag.win_id);
                        else
                            wm2_maximize(drag.win_id);
                    }
                }
                win->btn_hover = 0;
                draw_win(win);
            }
            drag.mode = DRAG_NONE;
        }
        return;
    }

    if (!btn_held) {
        static int prev_hover_id   = -1;
        static int prev_btn_hover  =  0;
        static int prev_btns_hov   =  0;
        wm2_win_t *hw    = topmost_at(mx, my);
        int        new_id    = hw ? hw->id : -1;
        int        new_hover = 0;
        int        new_grp   = 0;

        if (hw) {
            int h = hit_test_win(hw, mx, my);
            if (h >= WM2_HIT_BTN_CLOSE && h <= WM2_HIT_BTN_MAX)
                new_hover = h - WM2_HIT_BTN_CLOSE + 1;
            /* Check if mouse is in the button group bounding box */
            int rx = mx - hw->x, ry = my - hw->y;
            int grp_x0 = WM2_BTN_MARGIN - WM2_BTN_R - 4;
            int grp_x1 = WM2_BTN_MARGIN + 2 * WM2_BTN_SPACING + WM2_BTN_R + 4;
            int grp_y0 = WM2_TITLEBAR_H / 2 - WM2_BTN_R - 4;
            int grp_y1 = WM2_TITLEBAR_H / 2 + WM2_BTN_R + 4;
            if (rx >= grp_x0 && rx <= grp_x1 && ry >= grp_y0 && ry <= grp_y1)
                new_grp = 1;
        }

        if (new_id != prev_hover_id || new_hover != prev_btn_hover || new_grp != prev_btns_hov) {
            if (prev_hover_id != -1) {
                wm2_win_t *old = find_win(prev_hover_id);
                if (old) { old->btn_hover = 0; old->btns_hovered = 0; draw_win_buttons(old); }
            }
            if (hw) { hw->btn_hover = new_hover; hw->btns_hovered = new_grp; draw_win_buttons(hw); }
            prev_hover_id  = new_id;
            prev_btn_hover = new_hover;
            prev_btns_hov  = new_grp;
        }
    }

    if (btn_down) {
        int hit;
        win = topmost_at(mx, my);
        if (!win) return;

        if (!win->focused) wm2_focus(win->id);

        hit = hit_test_win(win, mx, my);

        if (hit >= WM2_HIT_BTN_CLOSE && hit <= WM2_HIT_BTN_MAX) {
            drag.mode   = DRAG_BTN;
            drag.win_id = win->id;
            drag.hit    = hit;
            win->btn_hover = hit - WM2_HIT_BTN_CLOSE + 1;
            draw_win(win);
            return;
        }

        if (hit == WM2_HIT_TITLEBAR) {
            drag.mode     = DRAG_MOVE;
            drag.win_id   = win->id;
            drag.start_mx = mx;     drag.start_my = my;
            drag.start_wx = win->x; drag.start_wy = win->y;
            drag.start_ww = win->w; drag.start_wh = win->h;
            return;
        }

        if (hit >= WM2_HIT_RESIZE_N) {
            drag.mode     = DRAG_RESIZE;
            drag.win_id   = win->id;
            drag.hit      = hit;
            drag.start_mx = mx;     drag.start_my = my;
            drag.start_wx = win->x; drag.start_wy = win->y;
            drag.start_ww = win->w; drag.start_wh = win->h;
            return;
        }
    }
}

void wm2_key_event(int id, char c) { (void)id; (void)c; }

/* ── Animated close ─────────────────────────────────────────────── */

void wm2_close_animated(int id) {
    wm2_win_t *win = find_win(id);
    if (!win || win->closing) return;
    win->closing = 1;
    win->close_alpha = 255;
    if (win->close_anim_id >= 0) anim_cancel(win->close_anim_id);
    win->close_anim_id = anim_start(&win->close_alpha, 255, 0, 140, ANIM_EASE_IN);
}

/* ── Animation tick ─────────────────────────────────────────────── */

void wm2_tick(void) {
    for (int i = 0; i < WM2_MAX_WINDOWS; i++) {
        wm2_win_t *win = &wins[i];
        if (!win->in_use || !win->surf) continue;

        /* Open fade-in */
        if (win->open_anim_id >= 0) {
            int a = win->open_alpha;
            if (a < 0) a = 0; if (a > 255) a = 255;
            comp_surface_set_alpha(win->surf, (uint8_t)a);
            if (!anim_active(win->open_anim_id)) {
                win->open_anim_id = -1;
                comp_surface_set_alpha(win->surf, 255);
            }
        }

        /* Close fade-out */
        if (win->closing && win->close_anim_id >= 0) {
            int a = win->close_alpha;
            if (a < 0) a = 0; if (a > 255) a = 255;
            comp_surface_set_alpha(win->surf, (uint8_t)a);
            if (!anim_active(win->close_anim_id)) {
                win->close_anim_id = -1;
                win->closing = 0;
                /* Actually destroy now */
                wm2_destroy(win->id);
                extern void menubar_update_windows(void);
                menubar_update_windows();
                continue; /* win is freed, skip rest */
            }
        }

        /* Minimize fade-out */
        if (win->minimizing && win->min_anim_id >= 0) {
            int a = win->min_alpha;
            if (a < 0) a = 0; if (a > 255) a = 255;
            comp_surface_set_alpha(win->surf, (uint8_t)a);
            if (!anim_active(win->min_anim_id)) {
                win->min_anim_id = -1;
                win->minimizing = 0;
                comp_surface_set_visible(win->surf, 0);
                extern void menubar_update_windows(void);
                menubar_update_windows();
            }
        }

        /* Restore fade-in */
        if (win->restoring && win->rest_anim_id >= 0) {
            int a = win->rest_alpha;
            if (a < 0) a = 0; if (a > 255) a = 255;
            comp_surface_set_alpha(win->surf, (uint8_t)a);
            if (!anim_active(win->rest_anim_id)) {
                win->rest_anim_id = -1;
                win->restoring = 0;
                comp_surface_set_alpha(win->surf, 255);
                extern void menubar_update_windows(void);
                menubar_update_windows();
            }
        }
    }
}
