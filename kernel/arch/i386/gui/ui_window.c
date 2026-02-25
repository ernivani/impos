/* ui_window.c — UIKit window protocol (Phase 4)
 *
 * Single-file window manager.  Replaces wm2.c.
 * Keeps the same comp_surface_t architecture; chrome is drawn
 * directly into the surface so the compositor handles blending.
 */

#include <kernel/ui_window.h>
#include <kernel/compositor.h>
#include <kernel/gfx.h>
#include <kernel/ui_token.h>
#include <kernel/ui_font.h>
#include <string.h>

/* ── Geometry constants ─────────────────────────────────────────── */
#define WIN_MAX         32
#define WIN_TITLEBAR_H  TOK_TITLEBAR_H   /* 38 px                  */
#define WIN_RADIUS      TOK_RADIUS_WIN   /* 12 px                  */
#define WIN_RESIZE_ZONE  6               /* invisible resize handle */
#define WIN_BTN_R        7               /* traffic-light radius    */
#define WIN_BTN_PAD_L   14              /* close-btn centre x      */
#define WIN_BTN_GAP     22              /* centre-to-centre        */

/* Pre-mixed colours (% white/black over dark window surfaces)      */
#define COL_BORDER   GFX_RGB( 42,  47,  58)  /* 10% white / WIN_BODY  */
#define COL_SEP      GFX_RGB( 32,  38,  54)  /* titlebar / body edge  */
#define COL_SYM      GFX_RGB(200, 200, 200)  /* hover symbols         */

/* Mouse-button flag (bit 0 = left) */
#define MOUSE_BTN_LEFT 0x01

/* ── Types ──────────────────────────────────────────────────────── */

typedef enum {
    DRAG_NONE,
    DRAG_MOVE,
    DRAG_RESIZE,
    DRAG_BTN_CLOSE,
    DRAG_BTN_MIN,
    DRAG_BTN_MAX
} drag_mode_t;

typedef struct {
    int    id;
    int    in_use;
    char   title[128];

    int    x, y, w, h;   /* outer bounds in screen coords          */
    int    sx, sy, sw, sh; /* saved for restore-from-maximise       */

    int    state;          /* UI_WIN_*                              */
    int    focused;
    int    visible;

    int    anim_alpha;     /* 0-255                                 */
    int    opening;        /* fading in                             */
    int    closing;        /* fading out → destroy on completion    */

    comp_surface_t *surf;  /* full window surface (chrome + client) */

    uint8_t close_hover, min_hover, max_hover;
    int     close_req;
} ui_win_t;

/* ── State ──────────────────────────────────────────────────────── */

static ui_win_t wins[WIN_MAX];
static int z_order[WIN_MAX]; /* window IDs, 0 = bottom             */
static int z_count  = 0;
static int focus_id = -1;

static struct {
    drag_mode_t mode;
    int         win_id;
    int         start_mx, start_my;
    int         start_wx, start_wy;
    int         start_ww, start_wh;
    int         resize_edge; /* WM2_HIT_RESIZE_* */
} drag;

/* ── Z-order helpers ────────────────────────────────────────────── */

static void z_add(int id)
{
    if (z_count < WIN_MAX)
        z_order[z_count++] = id;
}

static void z_remove(int id)
{
    for (int i = 0; i < z_count; i++) {
        if (z_order[i] == id) {
            memmove(&z_order[i], &z_order[i + 1],
                    (size_t)(z_count - i - 1) * sizeof(int));
            z_count--;
            return;
        }
    }
}

static void z_raise(int id)
{
    z_remove(id);
    z_add(id);
}

/* ── Hit-test ───────────────────────────────────────────────────── */

static int hit_test(const ui_win_t *w, int mx, int my)
{
    int R = WIN_RESIZE_ZONE;
    int wx = w->x, wy = w->y, ww = w->w, wh = w->h;

    if (mx < wx - R || mx >= wx + ww + R ||
        my < wy - R || my >= wy + wh + R)
        return UI_WIN_HIT_NONE;

    int on_n = (my >= wy - R && my < wy + R);
    int on_s = (my >= wy + wh - R && my < wy + wh + R);
    int on_w = (mx >= wx - R && mx < wx + R);
    int on_e = (mx >= wx + ww - R && mx < wx + ww + R);

    if (on_n && on_w) return UI_WIN_HIT_RESIZE_NW;
    if (on_n && on_e) return UI_WIN_HIT_RESIZE_NE;
    if (on_s && on_w) return UI_WIN_HIT_RESIZE_SW;
    if (on_s && on_e) return UI_WIN_HIT_RESIZE_SE;
    if (on_n)         return UI_WIN_HIT_RESIZE_N;
    if (on_s)         return UI_WIN_HIT_RESIZE_S;
    if (on_w)         return UI_WIN_HIT_RESIZE_W;
    if (on_e)         return UI_WIN_HIT_RESIZE_E;

    if (mx < wx || mx >= wx + ww || my < wy || my >= wy + wh)
        return UI_WIN_HIT_NONE;

    if (my < wy + WIN_TITLEBAR_H) {
        int by = WIN_TITLEBAR_H / 2;
        int bx0 = WIN_BTN_PAD_L;
        int bx1 = bx0 + WIN_BTN_GAP;
        int bx2 = bx1 + WIN_BTN_GAP;
        int HR  = WIN_BTN_R + 3;
        int dx, dy;

        dx = (mx - wx) - bx0; dy = (my - wy) - by;
        if (dx*dx + dy*dy <= HR*HR) return UI_WIN_HIT_BTN_CLOSE;
        dx = (mx - wx) - bx1;
        if (dx*dx + dy*dy <= HR*HR) return UI_WIN_HIT_BTN_MIN;
        dx = (mx - wx) - bx2;
        if (dx*dx + dy*dy <= HR*HR) return UI_WIN_HIT_BTN_MAX;

        return UI_WIN_HIT_TITLEBAR;
    }
    return UI_WIN_HIT_CONTENT;
}

/* ── Top visible window at (mx, my) ────────────────────────────── */

static ui_win_t *top_win_at(int mx, int my)
{
    for (int i = z_count - 1; i >= 0; i--) {
        ui_win_t *w = &wins[z_order[i]];
        if (!w->in_use || !w->visible ||
            w->state == UI_WIN_MINIMIZED) continue;
        if (hit_test(w, mx, my) != UI_WIN_HIT_NONE) return w;
    }
    return NULL;
}

/* ── Chrome draw ────────────────────────────────────────────────── */

static void draw_button(gfx_surface_t *s, int cx, int cy,
                        uint32_t fill, int hovered, uint32_t sym1x1,
                        int sym_type /* 0=× 1=- 2=□ */)
{
    gfx_surf_fill_circle(s, cx, cy, WIN_BTN_R, fill);

    if (hovered) {
        int hs = 3; /* half-symbol size */
        if (sym_type == 0) {                          /* × */
            gfx_surf_draw_line(s, cx-hs, cy-hs, cx+hs, cy+hs, sym1x1);
            gfx_surf_draw_line(s, cx+hs, cy-hs, cx-hs, cy+hs, sym1x1);
        } else if (sym_type == 1) {                   /* − */
            gfx_surf_fill_rect(s, cx-hs, cy, hs*2+1, 1, sym1x1);
        } else {                                      /* □ */
            gfx_surf_draw_rect(s, cx-hs, cy-hs, hs*2+1, hs*2+1, sym1x1);
        }
    }
}

static void win_redraw(ui_win_t *w)
{
    if (!w->surf) return;
    gfx_surface_t s = comp_surface_lock(w->surf);
    int sw = w->w, sh = w->h;
    int R  = WIN_RADIUS;
    int TH = WIN_TITLEBAR_H;

    /* 1. Clear to transparent */
    gfx_surf_fill_rect(&s, 0, 0, sw, sh, 0);

    /* 2. Full window shape in body colour (handles rounded corners) */
    gfx_surf_rounded_rect(&s, 0, 0, sw, sh, R, TOK_WIN_BODY);

    /* 3. Title bar: colour the top section using two-pass trick.
          Draw titlebar-colour rounded rect extending R below the bar,
          then restore body colour over that R-pixel overlap strip.    */
    gfx_surf_rounded_rect(&s, 0, 0, sw, TH + R, R, TOK_WIN_TITLEBAR);
    gfx_surf_fill_rect   (&s, 0, TH, sw, R, TOK_WIN_BODY);

    /* 4. Separator (1 px) at titlebar / content boundary */
    gfx_surf_fill_rect(&s, 0, TH, sw, 1, COL_SEP);

    /* 5. Title text — centred, protected from button area (left 80 px) */
    {
        int tpx = 13;
        int tw  = ui_font_width(w->title, tpx);
        int tx  = (sw - tw) / 2;
        if (tx < 80) tx = 80;
        int ty  = (TH - ui_font_height(tpx)) / 2;
        uint32_t tcol = w->focused ? TOK_TEXT_PRIMARY : TOK_TEXT_DIM;
        ui_font_draw(&s, tx, ty, w->title, tcol, tpx);
    }

    /* 6. Traffic lights */
    {
        int by   = TH / 2;
        int bx0  = WIN_BTN_PAD_L;
        int bx1  = bx0 + WIN_BTN_GAP;
        int bx2  = bx1 + WIN_BTN_GAP;
        uint32_t sym = COL_SYM;

        uint32_t close_c = w->close_hover
            ? GFX_RGB(255, 100, 88) : TOK_BTN_CLOSE;
        uint32_t min_c   = w->min_hover
            ? GFX_RGB(255, 200,  60) : TOK_BTN_MIN;
        uint32_t max_c   = w->max_hover
            ? GFX_RGB( 60, 220,  80) : TOK_BTN_MAX;

        draw_button(&s, bx0, by, close_c, w->close_hover, sym, 0);
        draw_button(&s, bx1, by, min_c,   w->min_hover,   sym, 1);
        draw_button(&s, bx2, by, max_c,   w->max_hover,   sym, 2);
    }

    /* 7. 1-px rounded border */
    gfx_surf_rounded_rect_outline(&s, 0, 0, sw, sh, R, COL_BORDER);

    comp_surface_damage_all(w->surf);
}

/* ── Public API ─────────────────────────────────────────────────── */

void ui_window_init(void)
{
    memset(wins,    0, sizeof(wins));
    memset(z_order, 0, sizeof(z_order));
    memset(&drag,   0, sizeof(drag));
    z_count  = 0;
    focus_id = -1;
    drag.win_id = -1;
}

int ui_window_create(int x, int y, int w, int h, const char *title)
{
    for (int i = 0; i < WIN_MAX; i++) {
        if (wins[i].in_use) continue;

        ui_win_t *win = &wins[i];
        memset(win, 0, sizeof(*win));
        win->id      = i;
        win->in_use  = 1;
        win->x = x; win->y = y; win->w = w; win->h = h;
        win->state   = UI_WIN_NORMAL;
        win->visible = 1;
        win->anim_alpha = 0;
        win->opening    = 1;

        strncpy(win->title, title ? title : "Window", 127);
        win->title[127] = '\0';

        win->surf = comp_surface_create(w, h, COMP_LAYER_WINDOWS);
        if (!win->surf) { win->in_use = 0; return -1; }

        comp_surface_move(win->surf, x, y);
        comp_surface_set_alpha(win->surf, 0);
        comp_surface_set_visible(win->surf, 1);

        z_add(i);
        ui_window_focus(i);
        win_redraw(win);
        return i;
    }
    return -1;
}

void ui_window_destroy(int id)
{
    if (id < 0 || id >= WIN_MAX || !wins[id].in_use) return;
    ui_win_t *w = &wins[id];

    if (w->surf) { comp_surface_destroy(w->surf); w->surf = NULL; }
    z_remove(id);

    if (focus_id == id) {
        focus_id = (z_count > 0) ? z_order[z_count - 1] : -1;
        if (focus_id >= 0) wins[focus_id].focused = 1;
    }

    memset(w, 0, sizeof(*w));
}

void ui_window_focus(int id)
{
    if (id < 0 || id >= WIN_MAX || !wins[id].in_use) return;
    if (focus_id >= 0 && focus_id != id && wins[focus_id].in_use) {
        wins[focus_id].focused = 0;
        win_redraw(&wins[focus_id]);
    }
    focus_id        = id;
    wins[id].focused = 1;
    win_redraw(&wins[id]);
}

void ui_window_raise(int id)
{
    if (id < 0 || id >= WIN_MAX || !wins[id].in_use) return;
    z_raise(id);
    comp_surface_raise(wins[id].surf);
    ui_window_focus(id);
}

void ui_window_move(int id, int x, int y)
{
    if (id < 0 || id >= WIN_MAX || !wins[id].in_use) return;
    wins[id].x = x; wins[id].y = y;
    comp_surface_move(wins[id].surf, x, y);
}

void ui_window_resize(int id, int w, int h)
{
    if (id < 0 || id >= WIN_MAX || !wins[id].in_use) return;
    ui_win_t *win = &wins[id];
    int min_w = 120, min_h = WIN_TITLEBAR_H + 40;
    if (w < min_w) w = min_w;
    if (h < min_h) h = min_h;
    win->w = w; win->h = h;
    comp_surface_resize(win->surf, w, h);
    win_redraw(win);
}

void ui_window_maximize(int id)
{
    if (id < 0 || id >= WIN_MAX || !wins[id].in_use) return;
    ui_win_t *w = &wins[id];
    if (w->state == UI_WIN_MAXIMIZED) return;
    w->sx = w->x; w->sy = w->y; w->sw = w->w; w->sh = w->h;
    w->state = UI_WIN_MAXIMIZED;
    int sw = (int)gfx_width(), sh = (int)gfx_height();
    ui_window_move(id, 0, TOK_MENUBAR_H);
    ui_window_resize(id, sw, sh - TOK_MENUBAR_H);
}

void ui_window_restore(int id)
{
    if (id < 0 || id >= WIN_MAX || !wins[id].in_use) return;
    ui_win_t *w = &wins[id];
    if (w->state == UI_WIN_NORMAL) return;
    w->state = UI_WIN_NORMAL;
    w->visible = 1;
    comp_surface_set_visible(w->surf, 1);
    ui_window_move(id, w->sx, w->sy);
    ui_window_resize(id, w->sw, w->sh);
}

void ui_window_minimize(int id)
{
    if (id < 0 || id >= WIN_MAX || !wins[id].in_use) return;
    ui_win_t *w = &wins[id];
    w->state   = UI_WIN_MINIMIZED;
    w->closing = 1;
    if (focus_id == id) {
        focus_id = (z_count > 1) ? z_order[z_count - 2] : -1;
        if (focus_id >= 0) ui_window_focus(focus_id);
    }
}

/* ── Queries ────────────────────────────────────────────────────── */

int ui_window_focused(void) { return focus_id; }

int ui_window_count(void)
{
    int n = 0;
    for (int i = 0; i < WIN_MAX; i++)
        if (wins[i].in_use) n++;
    return n;
}

ui_win_info_t ui_window_info(int id)
{
    ui_win_info_t info;
    memset(&info, 0, sizeof(info));
    if (id < 0 || id >= WIN_MAX || !wins[id].in_use) return info;
    ui_win_t *w = &wins[id];
    info.id = id;
    info.x = w->x; info.y = w->y; info.w = w->w; info.h = w->h;
    info.cx = w->x; info.cy = w->y + WIN_TITLEBAR_H;
    info.cw = w->w; info.ch = w->h - WIN_TITLEBAR_H;
    strncpy(info.title, w->title, 63); info.title[63] = '\0';
    info.state   = w->state;
    info.focused = w->focused;
    return info;
}

/* ── Canvas API ─────────────────────────────────────────────────── */

uint32_t *ui_window_canvas(int id, int *out_w, int *out_h)
{
    if (id < 0 || id >= WIN_MAX || !wins[id].in_use) return NULL;
    ui_win_t *w = &wins[id];
    if (!w->surf) return NULL;
    if (out_w) *out_w = w->w;
    if (out_h) *out_h = w->h - WIN_TITLEBAR_H;
    return w->surf->pixels + (size_t)WIN_TITLEBAR_H * (size_t)w->w;
}

void ui_window_damage(int id, int x, int y, int w, int h)
{
    if (id < 0 || id >= WIN_MAX || !wins[id].in_use) return;
    comp_surface_damage(wins[id].surf, x, y + WIN_TITLEBAR_H, w, h);
}

void ui_window_damage_all(int id)
{
    if (id < 0 || id >= WIN_MAX || !wins[id].in_use) return;
    comp_surface_damage_all(wins[id].surf);
}

/* ── Close ──────────────────────────────────────────────────────── */

int ui_window_close_requested(int id)
{
    if (id < 0 || id >= WIN_MAX || !wins[id].in_use) return 0;
    return wins[id].close_req;
}

void ui_window_close_clear(int id)
{
    if (id >= 0 && id < WIN_MAX) wins[id].close_req = 0;
}

void ui_window_close_animated(int id)
{
    if (id < 0 || id >= WIN_MAX || !wins[id].in_use) return;
    wins[id].closing = 1;
}

/* ── Key event ──────────────────────────────────────────────────── */

void ui_window_key_event(int id, char c)
{
    (void)id; (void)c; /* Phase 5: route to focused window's app   */
}

/* ── Redraw helpers ─────────────────────────────────────────────── */

void ui_window_redraw(int id)
{
    if (id >= 0 && id < WIN_MAX && wins[id].in_use)
        win_redraw(&wins[id]);
}

void ui_window_redraw_all(void)
{
    for (int i = 0; i < WIN_MAX; i++)
        if (wins[i].in_use) win_redraw(&wins[i]);
}

void ui_window_set_all_visible(int visible)
{
    for (int i = 0; i < WIN_MAX; i++) {
        if (!wins[i].in_use || wins[i].state == UI_WIN_MINIMIZED) continue;
        wins[i].visible = visible;
        comp_surface_set_visible(wins[i].surf, visible);
    }
}

/* ── Mouse event ────────────────────────────────────────────────── */

void ui_window_mouse_event(int mx, int my, uint8_t btns, uint8_t prev_btns)
{
    int btn_down = (btns & MOUSE_BTN_LEFT) && !(prev_btns & MOUSE_BTN_LEFT);
    int btn_up   = !(btns & MOUSE_BTN_LEFT) && (prev_btns & MOUSE_BTN_LEFT);

    /* ── Button-up: resolve drag ────────────────────────────────── */
    if (btn_up && drag.mode != DRAG_NONE) {
        ui_win_t *w = (drag.win_id >= 0) ? &wins[drag.win_id] : NULL;

        if (w && w->in_use) {
            if (drag.mode == DRAG_BTN_CLOSE && w->close_hover)
                w->close_req = 1;
            else if (drag.mode == DRAG_BTN_MIN && w->min_hover)
                ui_window_minimize(drag.win_id);
            else if (drag.mode == DRAG_BTN_MAX && w->max_hover) {
                if (w->state == UI_WIN_MAXIMIZED)
                    ui_window_restore(drag.win_id);
                else
                    ui_window_maximize(drag.win_id);
            }
        }

        drag.mode   = DRAG_NONE;
        drag.win_id = -1;
        return;
    }

    /* ── Update hover state ─────────────────────────────────────── */
    for (int i = 0; i < WIN_MAX; i++) {
        if (!wins[i].in_use || !wins[i].visible) continue;
        ui_win_t *w = &wins[i];
        int  h = hit_test(w, mx, my);
        int  prev_c = w->close_hover, prev_n = w->min_hover,
             prev_x = w->max_hover;

        w->close_hover = (h == UI_WIN_HIT_BTN_CLOSE) ? 1 : 0;
        w->min_hover   = (h == UI_WIN_HIT_BTN_MIN)   ? 1 : 0;
        w->max_hover   = (h == UI_WIN_HIT_BTN_MAX)   ? 1 : 0;

        if (w->close_hover != prev_c || w->min_hover != prev_n ||
            w->max_hover   != prev_x)
            win_redraw(w);
    }

    /* ── Active drag: move / resize ─────────────────────────────── */
    if ((btns & MOUSE_BTN_LEFT) && drag.mode != DRAG_NONE &&
        drag.win_id >= 0 && wins[drag.win_id].in_use) {

        int dx = mx - drag.start_mx;
        int dy = my - drag.start_my;

        if (drag.mode == DRAG_MOVE) {
            int nx = drag.start_wx + dx;
            int ny = drag.start_wy + dy;
            if (ny < TOK_MENUBAR_H) ny = TOK_MENUBAR_H;
            ui_window_move(drag.win_id, nx, ny);

        } else if (drag.mode == DRAG_RESIZE) {
            int nx = drag.start_wx, ny = drag.start_wy;
            int nw = drag.start_ww, nh = drag.start_wh;
            int e  = drag.resize_edge;

            if (e == UI_WIN_HIT_RESIZE_E  || e == UI_WIN_HIT_RESIZE_NE ||
                e == UI_WIN_HIT_RESIZE_SE) nw += dx;
            if (e == UI_WIN_HIT_RESIZE_W  || e == UI_WIN_HIT_RESIZE_NW ||
                e == UI_WIN_HIT_RESIZE_SW) { nx += dx; nw -= dx; }
            if (e == UI_WIN_HIT_RESIZE_S  || e == UI_WIN_HIT_RESIZE_SE ||
                e == UI_WIN_HIT_RESIZE_SW) nh += dy;
            if (e == UI_WIN_HIT_RESIZE_N  || e == UI_WIN_HIT_RESIZE_NW ||
                e == UI_WIN_HIT_RESIZE_NE) { ny += dy; nh -= dy; }

            ui_window_move(drag.win_id, nx, ny);
            ui_window_resize(drag.win_id, nw, nh);
        }
        return;
    }

    /* ── Button-down: start drag ────────────────────────────────── */
    if (btn_down) {
        ui_win_t *w = top_win_at(mx, my);
        if (!w) return;

        ui_window_raise(w->id);
        int h = hit_test(w, mx, my);

        drag.start_mx = mx; drag.start_my = my;
        drag.start_wx = w->x; drag.start_wy = w->y;
        drag.start_ww = w->w; drag.start_wh = w->h;
        drag.win_id   = w->id;

        if (h == UI_WIN_HIT_BTN_CLOSE) { drag.mode = DRAG_BTN_CLOSE; }
        else if (h == UI_WIN_HIT_BTN_MIN) { drag.mode = DRAG_BTN_MIN; }
        else if (h == UI_WIN_HIT_BTN_MAX) { drag.mode = DRAG_BTN_MAX; }
        else if (h == UI_WIN_HIT_TITLEBAR &&
                 w->state != UI_WIN_MAXIMIZED) {
            drag.mode = DRAG_MOVE;
        } else if (h >= UI_WIN_HIT_RESIZE_N) {
            drag.mode       = DRAG_RESIZE;
            drag.resize_edge = h;
        } else {
            drag.mode   = DRAG_NONE;
            drag.win_id = -1;
        }
    }
}

/* ── Animation tick ─────────────────────────────────────────────── */

void ui_window_tick(void)
{
    for (int i = 0; i < WIN_MAX; i++) {
        ui_win_t *w = &wins[i];
        if (!w->in_use) continue;

        if (w->opening) {
            w->anim_alpha += 18;
            if (w->anim_alpha >= 255) { w->anim_alpha = 255; w->opening = 0; }
            comp_surface_set_alpha(w->surf, (uint8_t)w->anim_alpha);
        } else if (w->closing) {
            w->anim_alpha -= 18;
            if (w->anim_alpha <= 0) {
                w->anim_alpha = 0;
                if (w->state == UI_WIN_MINIMIZED) {
                    comp_surface_set_visible(w->surf, 0);
                    w->closing = 0;
                } else {
                    ui_window_destroy(i);
                }
                continue;
            }
            comp_surface_set_alpha(w->surf, (uint8_t)w->anim_alpha);
        }
    }
}
