#ifndef _KERNEL_WM_H
#define _KERNEL_WM_H

#include <stdint.h>

/* Legacy WM stub — kept for compatibility with tty.c, shell.c,
 * win32_user32.c, win32_gdi32.c, signal.c.
 * The real window manager is wm2. */

#define WM_MAX_WINDOWS 32

/* Dummy window type for win32 compat layer */
typedef struct {
    int id;
    int task_id;
    int close_requested;
    int dirty;
    char title[64];
    uint32_t *canvas;
    int cx, cy, cw, ch;
} wm_window_t;

/* ── No-op stubs ─────────────────────────────────────────────── */

static inline void wm_mark_dirty(void) {}
static inline void wm_mouse_idle(void) {}
static inline void wm_flush_pending(void) {}
static inline void wm_composite(void) {}

static inline int wm_create_window(int x, int y, int w, int h,
                                    const char *title) {
    (void)x; (void)y; (void)w; (void)h; (void)title;
    return -1;
}
static inline void wm_destroy_window(int id) { (void)id; }
static inline void wm_focus_window(int id) { (void)id; }
static inline void wm_minimize_window(int id) { (void)id; }
static inline void wm_maximize_window(int id) { (void)id; }

static inline wm_window_t *wm_get_window(int id) { (void)id; return (wm_window_t*)0; }
static inline wm_window_t *wm_get_window_by_index(int idx) { (void)idx; return (wm_window_t*)0; }
static inline int wm_get_focused_id(void) { return -1; }
static inline void wm_get_content_rect(int id, int *cx, int *cy, int *cw, int *ch) {
    (void)id; if (cx) *cx = 0; if (cy) *cy = 0; if (cw) *cw = 0; if (ch) *ch = 0;
}
static inline uint32_t *wm_get_canvas(int id, int *w, int *h) {
    (void)id; if (w) *w = 0; if (h) *h = 0; return (uint32_t*)0;
}

/* GDI drawing stubs */
static inline void wm_put_pixel(int id, int x, int y, uint32_t c) {
    (void)id; (void)x; (void)y; (void)c;
}
static inline void wm_draw_line(int id, int x0, int y0, int x1, int y1, uint32_t c) {
    (void)id; (void)x0; (void)y0; (void)x1; (void)y1; (void)c;
}
static inline void wm_draw_rect(int id, int x, int y, int w, int h, uint32_t c) {
    (void)id; (void)x; (void)y; (void)w; (void)h; (void)c;
}
static inline void wm_fill_rect(int id, int x, int y, int w, int h, uint32_t c) {
    (void)id; (void)x; (void)y; (void)w; (void)h; (void)c;
}
static inline void wm_fill_rounded_rect(int id, int x, int y, int w, int h, int r, uint32_t c) {
    (void)id; (void)x; (void)y; (void)w; (void)h; (void)r; (void)c;
}
static inline void wm_draw_char(int id, int x, int y, char ch, uint32_t fg, uint32_t bg) {
    (void)id; (void)x; (void)y; (void)ch; (void)fg; (void)bg;
}

/* Monitor/fps stubs */
static inline int wm_fps_enabled(void) { return 0; }
static inline int wm_get_fps(void) { return 0; }
static inline int wm_get_gpu_usage(void) { return 0; }
static inline void wm_toggle_fps(void) {}

#endif
