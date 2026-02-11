#include <kernel/wm.h>
#include <kernel/gfx.h>
#include <kernel/desktop.h>
#include <kernel/mouse.h>
#include <kernel/idt.h>
#include <kernel/ui_theme.h>
#include <kernel/tty.h>
#include <kernel/task.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ═══ Window state ═════════════════════════════════════════════ */

static wm_window_t windows[WM_MAX_WINDOWS];
static int win_order[WM_MAX_WINDOWS];  /* z-order: [0]=topmost */
static int win_count;
static int next_id = 1;

/* Dragging state */
static int dragging = -1;
static int drag_ox, drag_oy;
static uint8_t prev_buttons;

/* Resize state */
static int resizing = -1;
static int resize_edge;  /* bitmask: 1=left, 2=right, 4=top, 8=bottom */
static int resize_ox, resize_oy, resize_ow, resize_oh, resize_orig_x, resize_orig_y;

/* Close / dock action flags */
static volatile int close_requested;
static volatile int dock_action;

/* Dock hover state */
static int dock_hover_idx = -1;

/* Background draw callback */
static void (*bg_draw_fn)(void) = 0;

/* Post-composite callback */
static void (*post_composite_fn)(void) = 0;

/* Cached background (gradient + menubar + dock) */
static uint32_t *bg_cache = 0;
static int bg_cache_valid = 0;

/* Dirty flag: set when something visual changed, cleared after composite */
static volatile int composite_needed = 0;

/* Throttle compositing during drag/resize to ~30fps */
static uint32_t last_drag_composite_tick = 0;

/* FPS overlay */
static int fps_overlay_enabled = 0;
static uint32_t fps_frame_count = 0;
static uint32_t fps_last_tick = 0;
static uint32_t fps_display_value = 0;

void wm_initialize(void) {
    memset(windows, 0, sizeof(windows));
    for (int i = 0; i < WM_MAX_WINDOWS; i++)
        windows[i].task_id = -1;
    win_count = 0;
    next_id = 1;
    dragging = -1;
    resizing = -1;
    prev_buttons = 0;
    close_requested = 0;
    dock_action = 0;
    bg_cache_valid = 0;
    if (!bg_cache) {
        bg_cache = malloc(gfx_height() * gfx_pitch());
    }
    /* Track WM's own memory usage (bg_cache + backbuffer) */
    task_set_mem(TASK_WM, (int)(gfx_height() * gfx_pitch() * 2 / 1024));
}

void wm_invalidate_bg(void) {
    bg_cache_valid = 0;
    composite_needed = 1;
}

void wm_mark_dirty(void) {
    composite_needed = 1;
}

int wm_is_dirty(void) {
    return composite_needed;
}

static wm_window_t* find_window(int id) {
    for (int i = 0; i < win_count; i++)
        if (windows[i].id == id) return &windows[i];
    return 0;
}

int wm_create_window(int x, int y, int w, int h, const char *title) {
    if (win_count >= WM_MAX_WINDOWS) return -1;
    wm_window_t *win = &windows[win_count];
    memset(win, 0, sizeof(*win));
    win->id = next_id++;
    win->x = x; win->y = y;
    win->w = w; win->h = h;
    strncpy(win->title, title, 63);
    win->title[63] = '\0';
    win->flags = WM_WIN_VISIBLE | WM_WIN_RESIZABLE;
    win->min_w = 200;
    win->min_h = 100;

    /* Allocate canvas for content area */
    win->canvas_w = w - 2 * WM_BORDER;
    win->canvas_h = h - WM_TITLEBAR_H - WM_BORDER;
    if (win->canvas_w > 0 && win->canvas_h > 0) {
        win->canvas = (uint32_t *)malloc((size_t)win->canvas_w * (size_t)win->canvas_h * 4);
        if (win->canvas) {
            uint32_t body_bg = ui_theme.win_body_bg;
            int total = win->canvas_w * win->canvas_h;
            for (int i = 0; i < total; i++)
                win->canvas[i] = body_bg;
        }
    } else {
        win->canvas = 0;
        win->canvas_w = 0;
        win->canvas_h = 0;
    }

    /* Auto-register as a tracked process */
    win->task_id = task_register(title, 1, win->id);

    /* Track canvas memory on the task */
    if (win->task_id >= 0 && win->canvas_w > 0 && win->canvas_h > 0)
        task_set_mem(win->task_id, (win->canvas_w * win->canvas_h * 4) / 1024);

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

    /* Auto-unregister tracked process */
    if (windows[idx].task_id >= 0) {
        task_unregister(windows[idx].task_id);
        windows[idx].task_id = -1;
    }

    /* Free canvas */
    if (windows[idx].canvas) {
        free(windows[idx].canvas);
        windows[idx].canvas = 0;
    }

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

    /* Stop dragging/resizing if destroyed window was being manipulated */
    if (dragging == idx) dragging = -1;
    if (resizing == idx) resizing = -1;
    if (dragging > idx) dragging--;
    if (resizing > idx) resizing--;

    /* Focus topmost */
    if (win_count > 0) {
        for (int i = 0; i < win_count; i++)
            windows[i].flags &= ~WM_WIN_FOCUSED;
        windows[win_order[0]].flags |= WM_WIN_FOCUSED;
    }
}

void wm_focus_window(int id) {
    int idx = -1;
    for (int i = 0; i < win_count; i++) {
        if (windows[i].id == id) idx = i;
        windows[i].flags &= ~WM_WIN_FOCUSED;
    }
    if (idx < 0) return;
    windows[idx].flags |= WM_WIN_FOCUSED;

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
        if (windows[i].flags & WM_WIN_FOCUSED) return windows[i].id;
    return -1;
}

int wm_close_was_requested(void) { return close_requested; }
void wm_clear_close_request(void) { close_requested = 0; }
int wm_get_dock_action(void) { return dock_action; }
void wm_clear_dock_action(void) { dock_action = 0; }

int wm_get_dock_hover(void) { return dock_hover_idx; }

int wm_get_window_count(void) { return win_count; }
wm_window_t* wm_get_window_by_index(int idx) {
    if (idx < 0 || idx >= win_count) return 0;
    return &windows[idx];
}

int wm_get_task_id(int win_id) {
    wm_window_t *w = find_window(win_id);
    return w ? w->task_id : -1;
}

void wm_cycle_focus(void) {
    if (win_count < 2) return;
    /* Rotate z-order: move front window to back */
    int front = win_order[0];
    for (int i = 0; i < win_count - 1; i++)
        win_order[i] = win_order[i + 1];
    win_order[win_count - 1] = front;
    /* Update focus */
    for (int i = 0; i < win_count; i++)
        windows[i].flags &= ~WM_WIN_FOCUSED;
    /* Skip minimized windows at front */
    int new_front = win_order[0];
    windows[new_front].flags |= WM_WIN_FOCUSED;
    wm_composite();
}

/* ═══ Minimize / Maximize / Resize ═══════════════════════════ */

void wm_minimize_window(int id) {
    wm_window_t *w = find_window(id);
    if (!w) return;
    w->flags |= WM_WIN_MINIMIZED;
    w->flags &= ~WM_WIN_VISIBLE;
    /* Focus next visible window */
    for (int i = 0; i < win_count; i++) {
        int idx = win_order[i];
        if (!(windows[idx].flags & WM_WIN_MINIMIZED) && idx != (int)(w - windows)) {
            wm_focus_window(windows[idx].id);
            break;
        }
    }
}

void wm_maximize_window(int id) {
    wm_window_t *w = find_window(id);
    if (!w) return;
    if (w->flags & WM_WIN_MAXIMIZED) return;

    /* Save current geometry */
    w->restore_x = w->x; w->restore_y = w->y;
    w->restore_w = w->w; w->restore_h = w->h;

    w->flags |= WM_WIN_MAXIMIZED;

    int sw = (int)gfx_width();
    int sh = (int)gfx_height() - ui_theme.taskbar_height;
    wm_resize_window(id, sw, sh);
    w->x = 0; w->y = 0;
}

void wm_restore_window(int id) {
    wm_window_t *w = find_window(id);
    if (!w) return;

    if (w->flags & WM_WIN_MINIMIZED) {
        w->flags &= ~WM_WIN_MINIMIZED;
        w->flags |= WM_WIN_VISIBLE;
        wm_focus_window(id);
        return;
    }

    if (w->flags & WM_WIN_MAXIMIZED) {
        w->flags &= ~WM_WIN_MAXIMIZED;
        w->x = w->restore_x; w->y = w->restore_y;
        wm_resize_window(id, w->restore_w, w->restore_h);
    }
}

int wm_is_minimized(int id) {
    wm_window_t *w = find_window(id);
    return w ? (w->flags & WM_WIN_MINIMIZED) != 0 : 0;
}

int wm_is_maximized(int id) {
    wm_window_t *w = find_window(id);
    return w ? (w->flags & WM_WIN_MAXIMIZED) != 0 : 0;
}

void wm_resize_window(int id, int new_w, int new_h) {
    wm_window_t *w = find_window(id);
    if (!w) return;

    /* Enforce minimum size */
    if (new_w < w->min_w) new_w = w->min_w;
    if (new_h < w->min_h) new_h = w->min_h;

    int new_cw = new_w - 2 * WM_BORDER;
    int new_ch = new_h - WM_TITLEBAR_H - WM_BORDER;
    if (new_cw <= 0 || new_ch <= 0) return;

    /* Allocate new canvas */
    uint32_t *new_canvas = (uint32_t *)malloc((size_t)new_cw * (size_t)new_ch * 4);
    if (!new_canvas) return;

    /* Fill with background */
    int total = new_cw * new_ch;
    for (int i = 0; i < total; i++)
        new_canvas[i] = ui_theme.win_bg;

    /* Copy old content (clipped to overlap) */
    if (w->canvas) {
        int copy_w = (new_cw < w->canvas_w) ? new_cw : w->canvas_w;
        int copy_h = (new_ch < w->canvas_h) ? new_ch : w->canvas_h;
        for (int row = 0; row < copy_h; row++)
            memcpy(&new_canvas[row * new_cw],
                   &w->canvas[row * w->canvas_w],
                   (size_t)copy_w * 4);
        free(w->canvas);
    }

    w->canvas = new_canvas;
    w->canvas_w = new_cw;
    w->canvas_h = new_ch;
    w->w = new_w;
    w->h = new_h;

    /* Update memory tracking for this window's task */
    if (w->task_id >= 0)
        task_set_mem(w->task_id, (new_cw * new_ch * 4) / 1024);

    /* Update terminal if it's bound to this window's canvas */
    terminal_notify_canvas_resize(id, new_canvas, new_cw, new_ch);
}

/* ═══ Drawing ═════════════════════════════════════════════════ */

static void draw_window(wm_window_t *w) {
    if (!(w->flags & WM_WIN_VISIBLE) || (w->flags & WM_WIN_MINIMIZED)) return;
    int focused = (w->flags & WM_WIN_FOCUSED) != 0;

    /* Cheap shadow: thin edge strips (right + bottom) instead of full rect */
    uint32_t shc = GFX_RGB(8, 8, 12);
    gfx_fill_rect(w->x + w->w, w->y + 2, 2, w->h, shc);       /* right */
    gfx_fill_rect(w->x + 2, w->y + w->h, w->w, 2, shc);       /* bottom */

    /* Window body — skip area covered by canvas */
    int body_y = w->y + WM_TITLEBAR_H;
    int body_h = w->h - WM_TITLEBAR_H;
    if (w->canvas && body_h > 0) {
        /* Only fill left/right border strips beside the canvas */
        if (WM_BORDER > 0) {
            gfx_fill_rect(w->x, body_y, WM_BORDER, body_h, ui_theme.win_body_bg);
            gfx_fill_rect(w->x + w->w - WM_BORDER, body_y, WM_BORDER, body_h, ui_theme.win_body_bg);
        }
        /* Bottom border strip below canvas */
        int canvas_bot = body_y + w->canvas_h;
        int bottom_strip = w->y + w->h - canvas_bot;
        if (bottom_strip > 0)
            gfx_fill_rect(w->x, canvas_bot, w->w, bottom_strip, ui_theme.win_body_bg);
    } else {
        gfx_fill_rect(w->x, w->y, w->w, w->h, ui_theme.win_body_bg);
    }

    /* Title bar */
    uint32_t hdr = focused ? ui_theme.win_header_focused : ui_theme.win_header_bg;
    gfx_fill_rect(w->x, w->y, w->w, WM_TITLEBAR_H, hdr);

    /* Separator line below titlebar */
    gfx_fill_rect(w->x, w->y + WM_TITLEBAR_H - 1, w->w, 1,
                  focused ? GFX_RGB(60, 60, 80) : ui_theme.win_border);

    /* Blit canvas content into body area */
    if (w->canvas)
        gfx_blit_buffer(w->x + WM_BORDER, w->y + WM_TITLEBAR_H,
                         w->canvas, w->canvas_w, w->canvas_h);

    /* Window border */
    gfx_draw_rect(w->x, w->y, w->w, w->h,
                  focused ? GFX_RGB(90, 90, 110) : ui_theme.win_border);

    /* macOS traffic light buttons */
    int btn_y = w->y + WM_TITLEBAR_H / 2;
    int r = WM_BTN_R;

    int close_cx = w->x + 8 + r;
    gfx_fill_circle(close_cx, btn_y, r, focused ? 0xFC615D : GFX_RGB(70, 65, 75));

    int min_cx = close_cx + 20;
    gfx_fill_circle(min_cx, btn_y, r, focused ? 0xFDBB2D : GFX_RGB(70, 65, 75));

    int max_cx = min_cx + 20;
    gfx_fill_circle(max_cx, btn_y, r, focused ? 0x27CA40 : GFX_RGB(70, 65, 75));

    /* Title text (centered) */
    int tw = (int)strlen(w->title) * FONT_W;
    int tx = w->x + (w->w - tw) / 2;
    int title_area_left = max_cx + r + 8;
    if (tx < title_area_left) tx = title_area_left;
    int ty = w->y + (WM_TITLEBAR_H - FONT_H) / 2;
    uint32_t tc = focused ? ui_theme.win_header_text : ui_theme.text_sub;
    gfx_draw_string(tx, ty, w->title, tc, hdr);
}

void wm_composite(void) {
    composite_needed = 0;
    uint32_t *bb = gfx_backbuffer();
    uint32_t total = gfx_height() * gfx_pitch();

    /* Build or restore cached background (gradient + menubar) */
    if (!bg_cache_valid && bg_cache) {
        gfx_clear(0);
        if (bg_draw_fn)
            bg_draw_fn();
        memcpy(bg_cache, bb, total);
        bg_cache_valid = 1;
    } else if (bg_cache) {
        memcpy(bb, bg_cache, total);
    } else {
        gfx_clear(0);
        if (bg_draw_fn)
            bg_draw_fn();
    }

    /* Draw windows back-to-front */
    for (int i = win_count - 1; i >= 0; i--) {
        draw_window(&windows[win_order[i]]);
    }

    /* Dock always on top of windows */
    desktop_draw_dock();

    /* Post-composite overlay (context menus, etc.) */
    if (post_composite_fn)
        post_composite_fn();

    /* FPS overlay (top-right corner) */
    if (fps_overlay_enabled) {
        fps_frame_count++;
        uint32_t now = pit_get_ticks();
        if (fps_last_tick == 0) fps_last_tick = now;
        if (now - fps_last_tick >= 120) {  /* every second at 120Hz PIT */
            fps_display_value = fps_frame_count * 120 / (now - fps_last_tick);
            fps_frame_count = 0;
            fps_last_tick = now;
        }
        char fps_buf[16];
        snprintf(fps_buf, sizeof(fps_buf), "FPS:%u", (unsigned)fps_display_value);
        int fw = (int)strlen(fps_buf) * FONT_W + 12;
        int fx = (int)gfx_width() - fw - 4;
        gfx_fill_rect(fx, 2, fw, FONT_H + 6, GFX_RGB(0, 0, 0));
        gfx_draw_string(fx + 6, 5, fps_buf, GFX_RGB(0, 255, 80), GFX_RGB(0, 0, 0));
    }

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
        if (!(w->flags & WM_WIN_VISIBLE) || (w->flags & WM_WIN_MINIMIZED)) continue;
        if (mx >= w->x && mx < w->x + w->w &&
            my >= w->y && my < w->y + w->h)
            return win_order[i];
    }
    return -1;
}

static int hit_close_button(wm_window_t *w, int mx, int my) {
    int cx = w->x + 8 + WM_BTN_R;
    int cy = w->y + WM_TITLEBAR_H / 2;
    int dx = mx - cx, dy = my - cy;
    return (dx * dx + dy * dy <= (WM_BTN_R + 2) * (WM_BTN_R + 2));
}

static int hit_minimize_button(wm_window_t *w, int mx, int my) {
    int cx = w->x + 8 + WM_BTN_R + 20;
    int cy = w->y + WM_TITLEBAR_H / 2;
    int dx = mx - cx, dy = my - cy;
    return (dx * dx + dy * dy <= (WM_BTN_R + 2) * (WM_BTN_R + 2));
}

static int hit_maximize_button(wm_window_t *w, int mx, int my) {
    int cx = w->x + 8 + WM_BTN_R + 40;
    int cy = w->y + WM_TITLEBAR_H / 2;
    int dx = mx - cx, dy = my - cy;
    return (dx * dx + dy * dy <= (WM_BTN_R + 2) * (WM_BTN_R + 2));
}

static int hit_titlebar(wm_window_t *w, int mx, int my) {
    return (mx >= w->x && mx < w->x + w->w &&
            my >= w->y && my < w->y + WM_TITLEBAR_H);
}

/* Detect resize edge (returns bitmask: 1=left, 2=right, 4=top, 8=bottom) */
static int hit_resize_edge(wm_window_t *w, int mx, int my) {
    if (!(w->flags & WM_WIN_RESIZABLE)) return 0;
    if (w->flags & WM_WIN_MAXIMIZED) return 0;
    int edge = 0;
    if (mx >= w->x && mx < w->x + WM_RESIZE_ZONE) edge |= 1;         /* left */
    if (mx >= w->x + w->w - WM_RESIZE_ZONE && mx < w->x + w->w) edge |= 2; /* right */
    if (my >= w->y && my < w->y + WM_RESIZE_ZONE) edge |= 4;         /* top */
    if (my >= w->y + w->h - WM_RESIZE_ZONE && my < w->y + w->h) edge |= 8; /* bottom */
    return edge;
}

/* Dock hit test — uses desktop.h geometry accessors */
static int dock_hit(int mx, int my) {
    int dy = desktop_dock_y();
    int dh = desktop_dock_h();
    int dx = desktop_dock_x();
    int dw = desktop_dock_w();

    if (my < dy || my > dy + dh) return -1;
    if (mx < dx || mx > dx + dw) return -1;

    int nitems = desktop_dock_items();
    for (int i = 0; i < nitems; i++) {
        int ix, iy, iw, ih;
        if (desktop_dock_item_rect(i, &ix, &iy, &iw, &ih)) {
            if (mx >= ix && mx < ix + iw && my >= iy && my < iy + ih)
                return i;
        }
    }
    return -1;
}

/* dock actions are now dynamic — use desktop_dock_action() */

void wm_mouse_idle(void) {
    if (!mouse_poll()) return;

    int mx = mouse_get_x(), my = mouse_get_y();
    uint8_t btns = mouse_get_buttons();
    int left_click = (btns & MOUSE_BTN_LEFT) && !(prev_buttons & MOUSE_BTN_LEFT);
    int left_release = !(btns & MOUSE_BTN_LEFT) && (prev_buttons & MOUSE_BTN_LEFT);
    prev_buttons = btns;

    /* Handle resize dragging */
    if (resizing >= 0) {
        if (btns & MOUSE_BTN_LEFT) {
            wm_window_t *w = &windows[resizing];
            int new_w = resize_ow, new_h = resize_oh;
            int new_x = w->x, new_y = w->y;

            if (resize_edge & 2) new_w = resize_ow + (mx - resize_ox); /* right */
            if (resize_edge & 1) { new_w = resize_ow - (mx - resize_ox); new_x = resize_orig_x + (mx - resize_ox); } /* left */
            if (resize_edge & 8) new_h = resize_oh + (my - resize_oy); /* bottom */
            if (resize_edge & 4) { new_h = resize_oh - (my - resize_oy); new_y = resize_orig_y + (my - resize_oy); } /* top */

            if (new_w >= w->min_w && new_h >= w->min_h) {
                uint32_t now = pit_get_ticks();
                if (now - last_drag_composite_tick >= 3) {
                    w->x = new_x; w->y = new_y;
                    wm_resize_window(w->id, new_w, new_h);
                    wm_composite();
                    last_drag_composite_tick = now;
                } else {
                    /* Still update cursor so it doesn't freeze between composites */
                    gfx_draw_mouse_cursor(mx, my);
                }
            }
        } else {
            resizing = -1;
            wm_composite();
        }
        return;
    }

    /* Handle window dragging */
    if (dragging >= 0) {
        if (btns & MOUSE_BTN_LEFT) {
            wm_window_t *w = &windows[dragging];
            w->x = mx - drag_ox;
            w->y = my - drag_oy;
            if (w->y < 0) w->y = 0;
            if (w->y > (int)gfx_height() - WM_TITLEBAR_H)
                w->y = (int)gfx_height() - WM_TITLEBAR_H;
            uint32_t now = pit_get_ticks();
            if (now - last_drag_composite_tick >= 3) {
                wm_composite();
                last_drag_composite_tick = now;
            } else {
                /* Still update cursor so it doesn't freeze between composites */
                gfx_draw_mouse_cursor(mx, my);
            }
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
        if (di >= 0) {
            int da_val = desktop_dock_action(di);
            if (da_val > 0) {
                dock_action = da_val;
            } else {
                /* Running app — focus its window */
                /* Encode as negative dock index for desktop to handle */
                dock_action = -(di + 1);
            }
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
            if (hit_minimize_button(w, mx, my)) {
                wm_minimize_window(w->id);
                wm_composite();
                return;
            }
            if (hit_maximize_button(w, mx, my)) {
                if (w->flags & WM_WIN_MAXIMIZED)
                    wm_restore_window(w->id);
                else
                    wm_maximize_window(w->id);
                wm_composite();
                return;
            }

            /* Check resize edges */
            int edge = hit_resize_edge(w, mx, my);
            if (edge) {
                resizing = widx;
                resize_edge = edge;
                resize_ox = mx; resize_oy = my;
                resize_ow = w->w; resize_oh = w->h;
                resize_orig_x = w->x; resize_orig_y = w->y;
                return;
            }

            if (hit_titlebar(w, mx, my)) {
                /* Double-click on titlebar could maximize — for now just drag */
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

    (void)left_release;

    /* Check dock hover on any mouse move */
    int new_hover = dock_hit(mx, my);
    if (new_hover < 0) new_hover = -1;
    if (new_hover != dock_hover_idx) {
        dock_hover_idx = new_hover;
        wm_composite();
        return;
    }

    /* Just cursor moved */
    gfx_draw_mouse_cursor(mx, my);
}

/* ═══ Canvas drawing wrappers ═════════════════════════════════ */

void wm_fill_rect(int win_id, int x, int y, int w, int h, uint32_t color) {
    wm_window_t *win = find_window(win_id);
    if (!win || !win->canvas) return;
    gfx_buf_fill_rect(win->canvas, win->canvas_w, win->canvas_h, x, y, w, h, color);
}

void wm_draw_string(int win_id, int x, int y, const char *s, uint32_t fg, uint32_t bg) {
    wm_window_t *win = find_window(win_id);
    if (!win || !win->canvas) return;
    gfx_buf_draw_string(win->canvas, win->canvas_w, win->canvas_h, x, y, s, fg, bg);
}

void wm_draw_char(int win_id, int x, int y, char c, uint32_t fg, uint32_t bg) {
    wm_window_t *win = find_window(win_id);
    if (!win || !win->canvas) return;
    gfx_buf_draw_char(win->canvas, win->canvas_w, win->canvas_h, x, y, c, fg, bg);
}

void wm_put_pixel(int win_id, int x, int y, uint32_t color) {
    wm_window_t *win = find_window(win_id);
    if (!win || !win->canvas) return;
    gfx_buf_put_pixel(win->canvas, win->canvas_w, win->canvas_h, x, y, color);
}

void wm_draw_rect(int win_id, int x, int y, int w, int h, uint32_t color) {
    wm_window_t *win = find_window(win_id);
    if (!win || !win->canvas) return;
    gfx_buf_draw_rect(win->canvas, win->canvas_w, win->canvas_h, x, y, w, h, color);
}

void wm_draw_line(int win_id, int x0, int y0, int x1, int y1, uint32_t color) {
    wm_window_t *win = find_window(win_id);
    if (!win || !win->canvas) return;
    gfx_buf_draw_line(win->canvas, win->canvas_w, win->canvas_h, x0, y0, x1, y1, color);
}

void wm_clear_canvas(int win_id, uint32_t color) {
    wm_window_t *win = find_window(win_id);
    if (!win || !win->canvas) return;
    int total = win->canvas_w * win->canvas_h;
    for (int i = 0; i < total; i++)
        win->canvas[i] = color;
}

uint32_t *wm_get_canvas(int win_id, int *out_w, int *out_h) {
    wm_window_t *win = find_window(win_id);
    if (!win || !win->canvas) {
        if (out_w) *out_w = 0;
        if (out_h) *out_h = 0;
        return 0;
    }
    if (out_w) *out_w = win->canvas_w;
    if (out_h) *out_h = win->canvas_h;
    return win->canvas;
}

void wm_fill_rounded_rect(int win_id, int x, int y, int w, int h, int r, uint32_t color) {
    wm_window_t *win = find_window(win_id);
    if (!win || !win->canvas) return;
    gfx_surface_t s = { win->canvas, win->canvas_w, win->canvas_h, win->canvas_w };
    gfx_surf_rounded_rect(&s, x, y, w, h, r, color);
}

void wm_fill_rounded_rect_alpha(int win_id, int x, int y, int w, int h, int r, uint32_t color, uint8_t a) {
    wm_window_t *win = find_window(win_id);
    if (!win || !win->canvas) return;
    gfx_surface_t s = { win->canvas, win->canvas_w, win->canvas_h, win->canvas_w };
    gfx_surf_rounded_rect_alpha(&s, x, y, w, h, r, color, a);
}

void wm_set_bg_draw(void (*fn)(void)) {
    bg_draw_fn = fn;
}

void wm_set_post_composite(void (*fn)(void)) {
    post_composite_fn = fn;
}

int wm_hit_test(int mx, int my) {
    int idx = hit_test_window(mx, my);
    if (idx < 0) return -1;
    return windows[idx].id;
}

void wm_toggle_fps(void) {
    fps_overlay_enabled = !fps_overlay_enabled;
    fps_frame_count = 0;
    fps_last_tick = 0;
    fps_display_value = 0;
    wm_mark_dirty();
}

int wm_fps_enabled(void) {
    return fps_overlay_enabled;
}
