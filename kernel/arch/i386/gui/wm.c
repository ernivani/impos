#include <kernel/wm.h>
#include <kernel/gfx.h>
#include <kernel/desktop.h>
#include <kernel/mouse.h>
#include <kernel/idt.h>
#include <string.h>
#include <stdio.h>

/* ═══ Window state ═════════════════════════════════════════════ */

static wm_window_t windows[WM_MAX_WINDOWS];
static int win_order[WM_MAX_WINDOWS];  /* z-order: [0]=topmost */
static int win_count;
static int next_id = 1;

/* Dragging state */
static int dragging = -1;
static int drag_ox, drag_oy;
static uint8_t prev_buttons;

/* Close / dock action flags */
static volatile int close_requested;
static volatile int dock_action;

void wm_initialize(void) {
    memset(windows, 0, sizeof(windows));
    win_count = 0;
    next_id = 1;
    dragging = -1;
    prev_buttons = 0;
    close_requested = 0;
    dock_action = 0;
}

static wm_window_t* find_window(int id) {
    for (int i = 0; i < win_count; i++)
        if (windows[i].id == id) return &windows[i];
    return 0;
}

int wm_create_window(int x, int y, int w, int h, const char *title) {
    if (win_count >= WM_MAX_WINDOWS) return -1;
    wm_window_t *win = &windows[win_count];
    win->id = next_id++;
    win->x = x; win->y = y;
    win->w = w; win->h = h;
    strncpy(win->title, title, 63);
    win->title[63] = '\0';
    win->visible = 1;
    win->focused = 0;

    /* Push to front of z-order */
    for (int i = win_count; i > 0; i--)
        win_order[i] = win_order[i - 1];
    win_order[0] = win_count;
    win_count++;

    wm_focus_window(win->id);
    return win->id;
}

void wm_destroy_window(int id) {
    int idx = -1;
    for (int i = 0; i < win_count; i++)
        if (windows[i].id == id) { idx = i; break; }
    if (idx < 0) return;

    /* Remove from z-order */
    int zidx = -1;
    for (int i = 0; i < win_count; i++)
        if (win_order[i] == idx) { zidx = i; break; }
    if (zidx >= 0) {
        for (int i = zidx; i < win_count - 1; i++)
            win_order[i] = win_order[i + 1];
    }

    /* Remove window, shift remaining */
    for (int i = idx; i < win_count - 1; i++)
        windows[i] = windows[i + 1];
    win_count--;

    /* Fix z-order indices */
    for (int i = 0; i < win_count; i++) {
        if (win_order[i] > idx) win_order[i]--;
        else if (win_order[i] == idx) win_order[i] = 0;
    }

    /* Focus topmost */
    if (win_count > 0) {
        for (int i = 0; i < win_count; i++)
            windows[i].focused = 0;
        windows[win_order[0]].focused = 1;
    }
}

void wm_focus_window(int id) {
    int idx = -1;
    for (int i = 0; i < win_count; i++) {
        if (windows[i].id == id) idx = i;
        windows[i].focused = 0;
    }
    if (idx < 0) return;
    windows[idx].focused = 1;

    /* Move to front of z-order */
    int zidx = -1;
    for (int i = 0; i < win_count; i++)
        if (win_order[i] == idx) { zidx = i; break; }
    if (zidx > 0) {
        for (int i = zidx; i > 0; i--)
            win_order[i] = win_order[i - 1];
        win_order[0] = idx;
    }
}

void wm_get_content_rect(int id, int *cx, int *cy, int *cw, int *ch) {
    wm_window_t *w = find_window(id);
    if (!w) { *cx = *cy = *cw = *ch = 0; return; }
    *cx = w->x + WM_BORDER;
    *cy = w->y + WM_TITLEBAR_H;
    *cw = w->w - 2 * WM_BORDER;
    *ch = w->h - WM_TITLEBAR_H - WM_BORDER;
}

wm_window_t* wm_get_window(int id) { return find_window(id); }

int wm_get_focused_id(void) {
    for (int i = 0; i < win_count; i++)
        if (windows[i].focused) return windows[i].id;
    return -1;
}

int wm_close_was_requested(void) { return close_requested; }
void wm_clear_close_request(void) { close_requested = 0; }
int wm_get_dock_action(void) { return dock_action; }
void wm_clear_dock_action(void) { dock_action = 0; }

/* ═══ Drawing ═════════════════════════════════════════════════ */

static void draw_window(wm_window_t *w) {
    if (!w->visible) return;
    int focused = w->focused;

    /* Window body */
    gfx_fill_rect(w->x, w->y, w->w, w->h, WM_BODY_BG);

    /* Title bar */
    uint32_t hdr = focused ? WM_HEADER_FOCUSED : WM_HEADER_BG;
    gfx_fill_rect(w->x, w->y, w->w, WM_TITLEBAR_H, hdr);

    /* Bottom border of title bar */
    gfx_fill_rect(w->x, w->y + WM_TITLEBAR_H - 1, w->w, 1, WM_BORDER_COLOR);

    /* Window outer border */
    gfx_draw_rect(w->x, w->y, w->w, w->h,
                  focused ? GFX_RGB(80, 80, 80) : WM_BORDER_COLOR);

    /* Title text (centered) */
    int tw = (int)strlen(w->title) * FONT_W;
    int tx = w->x + w->w / 2 - tw / 2;
    int ty = w->y + (WM_TITLEBAR_H - FONT_H) / 2;
    gfx_draw_string(tx, ty, w->title, WM_HEADER_TEXT, hdr);

    /* Close button: red circle at right of title bar */
    int close_cx = w->x + w->w - 16;
    int close_cy = w->y + WM_TITLEBAR_H / 2;
    uint32_t cc = focused ? WM_CLOSE_NORMAL : GFX_RGB(100, 50, 50);
    for (int dy = -WM_BTN_R; dy <= WM_BTN_R; dy++)
        for (int dx = -WM_BTN_R; dx <= WM_BTN_R; dx++)
            if (dx * dx + dy * dy <= WM_BTN_R * WM_BTN_R)
                gfx_put_pixel(close_cx + dx, close_cy + dy, cc);
    /* X mark */
    uint32_t xc = GFX_RGB(255, 255, 255);
    for (int d = -2; d <= 2; d++) {
        gfx_put_pixel(close_cx + d, close_cy + d, xc);
        gfx_put_pixel(close_cx + d, close_cy - d, xc);
    }
}

void wm_composite(void) {
    /* Background */
    gfx_clear(0);

    /* Draw windows back-to-front */
    for (int i = win_count - 1; i >= 0; i--) {
        draw_window(&windows[win_order[i]]);
    }

    /* Dock */
    desktop_draw_dock();

    /* Flip to framebuffer */
    gfx_flip();

    /* Mouse cursor on top (directly to framebuffer) */
    gfx_draw_mouse_cursor(mouse_get_x(), mouse_get_y());
}

/* ═══ Mouse processing ═══════════════════════════════════════ */

/* Returns which window index is at screen (mx, my), or -1 */
static int hit_test_window(int mx, int my) {
    for (int i = 0; i < win_count; i++) {
        wm_window_t *w = &windows[win_order[i]];
        if (!w->visible) continue;
        if (mx >= w->x && mx < w->x + w->w &&
            my >= w->y && my < w->y + w->h)
            return win_order[i];
    }
    return -1;
}

static int hit_close_button(wm_window_t *w, int mx, int my) {
    int cx = w->x + w->w - 16;
    int cy = w->y + WM_TITLEBAR_H / 2;
    int dx = mx - cx, dy = my - cy;
    return (dx * dx + dy * dy <= (WM_BTN_R + 2) * (WM_BTN_R + 2));
}

static int hit_titlebar(wm_window_t *w, int mx, int my) {
    return (mx >= w->x && mx < w->x + w->w &&
            my >= w->y && my < w->y + WM_TITLEBAR_H);
}

/* Dock hit test — returns dock item index (0..5) or -2 for power, -1 for none */
static int dock_hit(int mx, int my) {
    int fb_w = (int)gfx_width(), fb_h = (int)gfx_height();
    int ty = fb_h - TASKBAR_H;
    if (my < ty) return -1;

    /* Power icon */
    int pr_x = fb_w - 28, pr_y = ty + 14;
    if (mx >= pr_x && mx < pr_x + 20 && my >= pr_y && my < pr_y + 20)
        return -2;

    /* Dock pill */
    int item_w = 34, gap = 2, sep_w = 12;
    int dock_items = 6;
    int dock_sep = 4;
    int dock_w = dock_items * item_w + (dock_items - 1) * gap + sep_w + 16;
    int dock_x = fb_w / 2 - dock_w / 2;
    int dock_y = ty + 7;
    int dock_h = 34;

    if (my < dock_y || my > dock_y + dock_h) return -1;
    if (mx < dock_x || mx > dock_x + dock_w) return -1;

    int ix = dock_x + 8;
    for (int i = 0; i < dock_items; i++) {
        if (i == dock_sep) ix += sep_w;
        if (mx >= ix && mx < ix + item_w)
            return i;
        ix += item_w + gap;
    }
    return -1;
}

static const int dock_action_map[] = {
    DESKTOP_ACTION_FILES, DESKTOP_ACTION_TERMINAL, DESKTOP_ACTION_BROWSER,
    DESKTOP_ACTION_EDITOR, DESKTOP_ACTION_SETTINGS, DESKTOP_ACTION_MONITOR
};

void wm_mouse_idle(void) {
    if (!mouse_poll()) return;

    int mx = mouse_get_x(), my = mouse_get_y();
    uint8_t btns = mouse_get_buttons();
    int left_click = (btns & MOUSE_BTN_LEFT) && !(prev_buttons & MOUSE_BTN_LEFT);
    (void)(!(btns & MOUSE_BTN_LEFT) && (prev_buttons & MOUSE_BTN_LEFT)); /* left_release unused for now */
    prev_buttons = btns;

    /* Handle dragging */
    if (dragging >= 0) {
        if (btns & MOUSE_BTN_LEFT) {
            wm_window_t *w = &windows[dragging];
            w->x = mx - drag_ox;
            w->y = my - drag_oy;
            /* Clamp so title bar stays visible */
            if (w->y < 0) w->y = 0;
            if (w->y > (int)gfx_height() - WM_TITLEBAR_H)
                w->y = (int)gfx_height() - WM_TITLEBAR_H;
            wm_composite();
        } else {
            dragging = -1;
            wm_composite();
        }
        return;
    }

    /* Left click */
    if (left_click) {
        /* Check dock first */
        int di = dock_hit(mx, my);
        if (di == -2) {
            dock_action = DESKTOP_ACTION_POWER;
            return;
        }
        if (di >= 0 && di < 6) {
            dock_action = dock_action_map[di];
            return;
        }

        /* Check windows (front to back) */
        int widx = hit_test_window(mx, my);
        if (widx >= 0) {
            wm_window_t *w = &windows[widx];
            wm_focus_window(w->id);

            if (hit_close_button(w, mx, my)) {
                close_requested = 1;
                return;
            }
            if (hit_titlebar(w, mx, my)) {
                dragging = widx;
                drag_ox = mx - w->x;
                drag_oy = my - w->y;
                wm_composite();
                return;
            }
            wm_composite();
            return;
        }
    }

    /* Just cursor moved — redraw cursor */
    gfx_draw_mouse_cursor(mx, my);
}
