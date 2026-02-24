/* wm.c — Phase 1 stub
 * Old compositor, shadow atlas, bg_cache, drag/resize state removed.
 * Will be replaced by compositor.c + wm2.c in Phase 2/3.
 */
#include <kernel/wm.h>
#include <kernel/gfx.h>
#include <kernel/ui_theme.h>
#include <kernel/mouse.h>
#include <kernel/desktop.h>
#include <kernel/io.h>
#include <string.h>
#include <stdlib.h>

static void (*bg_draw_fn)(void)         = 0;
static void (*post_composite_fn)(void)  = 0;
static int dirty = 0;

/* ── Init ───────────────────────────────────────────────────────── */

void wm_initialize(void) { dirty = 1; }

/* ── Composite: clear to desktop bg and flip ────────────────────── */

void wm_composite(void) {
    if (!gfx_is_active()) return;
    int w = gfx_width(), h = gfx_height();
    uint32_t *bb = gfx_backbuffer();
    uint32_t pitch4 = gfx_pitch() / 4;
    uint32_t bg = ui_theme.desktop_bg;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            bb[y * pitch4 + x] = bg;
    if (bg_draw_fn) bg_draw_fn();
    if (post_composite_fn) post_composite_fn();
    gfx_flip();
    dirty = 0;
}

void wm_flush_pending(void)   { if (dirty) wm_composite(); }
void wm_invalidate_bg(void)   { dirty = 1; }
void wm_mark_dirty(void)      { dirty = 1; }
int  wm_is_dirty(void)        { return dirty; }

/* ── Window management stubs (no real windows in Phase 1) ────────── */

int  wm_create_window(int x, int y, int w, int h, const char *title) {
    (void)x; (void)y; (void)w; (void)h; (void)title; return -1;
}
void wm_destroy_window(int id)          { (void)id; }
void wm_focus_window(int id)            { (void)id; }
void wm_set_opacity(int id, uint8_t o)  { (void)id; (void)o; }
void wm_minimize_window(int id)         { (void)id; }
void wm_maximize_window(int id)         { (void)id; }
void wm_restore_window(int id)          { (void)id; }
void wm_resize_window(int id, int w, int h) { (void)id; (void)w; (void)h; }
int  wm_is_minimized(int id)            { (void)id; return 0; }
int  wm_is_maximized(int id)            { (void)id; return 0; }
int  wm_get_focused_id(void)            { return -1; }
int  wm_get_window_count(void)          { return 0; }
int  wm_hit_test(int mx, int my)        { (void)mx; (void)my; return -1; }
int  wm_get_z_order_index(int z)        { (void)z; return -1; }
int  wm_get_task_id(int id)             { (void)id; return -1; }
int  wm_get_dock_hover(void)            { return -1; }
int  wm_get_dock_action(void)           { return 0; }
void wm_clear_dock_action(void)         { }
void wm_cycle_focus(void)               { }
int  wm_close_was_requested(void)       { return 0; }
void wm_clear_close_request(void)       { }
void wm_mouse_idle(void)                { }
void wm_toggle_fps(void)                { }
int  wm_fps_enabled(void)               { return 0; }
uint32_t wm_get_fps(void)               { return 0; }
uint32_t wm_get_gpu_usage(void)         { return 0; }

wm_window_t* wm_get_window(int id)              { (void)id; return 0; }
wm_window_t* wm_get_window_by_index(int idx)    { (void)idx; return 0; }

void wm_get_content_rect(int id, int *cx, int *cy, int *cw, int *ch) {
    (void)id;
    if (cx) *cx = 0; if (cy) *cy = 0;
    if (cw) *cw = 0; if (ch) *ch = 0;
}

/* Canvas drawing stubs (no windows = no canvas) */
void     wm_fill_rect(int id, int x, int y, int w, int h, uint32_t c)          { (void)id;(void)x;(void)y;(void)w;(void)h;(void)c; }
void     wm_draw_string(int id, int x, int y, const char *s, uint32_t f, uint32_t b) { (void)id;(void)x;(void)y;(void)s;(void)f;(void)b; }
void     wm_draw_char(int id, int x, int y, char c, uint32_t f, uint32_t b)    { (void)id;(void)x;(void)y;(void)c;(void)f;(void)b; }
void     wm_put_pixel(int id, int x, int y, uint32_t c)                        { (void)id;(void)x;(void)y;(void)c; }
void     wm_draw_rect(int id, int x, int y, int w, int h, uint32_t c)          { (void)id;(void)x;(void)y;(void)w;(void)h;(void)c; }
void     wm_draw_line(int id, int x0, int y0, int x1, int y1, uint32_t c)      { (void)id;(void)x0;(void)y0;(void)x1;(void)y1;(void)c; }
void     wm_clear_canvas(int id, uint32_t c)                                   { (void)id;(void)c; }
void     wm_fill_rounded_rect(int id, int x, int y, int w, int h, int r, uint32_t c)       { (void)id;(void)x;(void)y;(void)w;(void)h;(void)r;(void)c; }
void     wm_fill_rounded_rect_alpha(int id, int x, int y, int w, int h, int r, uint32_t c, uint8_t a) { (void)id;(void)x;(void)y;(void)w;(void)h;(void)r;(void)c;(void)a; }
uint32_t *wm_get_canvas(int id, int *ow, int *oh) { (void)id; if (ow)*ow=0; if (oh)*oh=0; return 0; }

void wm_set_bg_draw(void (*fn)(void))          { bg_draw_fn = fn; }
void wm_set_post_composite(void (*fn)(void))   { post_composite_fn = fn; }
