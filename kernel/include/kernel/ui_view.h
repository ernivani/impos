/* ui_view.h — UIKit view tree
 *
 * Every UI element (button, label, window, desktop) is a ui_view_t
 * node in a tree.  Views have:
 *   - A declared size/layout (what they want)
 *   - A computed screen rect (what they got, set by ui_layout)
 *   - A style + pseudo-state styles (hover/active/focus)
 *   - Event callbacks (on_click, on_hover, on_key, on_paint)
 *   - Up to UI_MAX_CHILDREN children
 *
 * Memory: static pool of UI_VIEW_POOL_SIZE nodes (no malloc per view).
 */

#ifndef _KERNEL_UI_VIEW_H
#define _KERNEL_UI_VIEW_H

#include <stdint.h>
#include <kernel/gfx.h>
#include <kernel/compositor.h>
#include <kernel/ui_token.h>
#include <kernel/ui_font.h>
#include <kernel/ui_fx.h>

/* ═══ Limits ═════════════════════════════════════════════════════ */

#define UI_VIEW_POOL_SIZE  256
#define UI_MAX_CHILDREN     32

/* ═══ Size modes ═════════════════════════════════════════════════ */

#define UI_SIZE_FIXED    0   /* exact pixel dimensions */
#define UI_SIZE_FILL     1   /* flex-grow: fill available space */
#define UI_SIZE_HUG      2   /* shrink-wrap: hug children */

/* ═══ Flex direction ═════════════════════════════════════════════ */

#define UI_DIR_ROW       0   /* children laid out left-to-right */
#define UI_DIR_COL       1   /* children laid out top-to-bottom */

/* ═══ Cross-axis alignment ═══════════════════════════════════════ */

#define UI_ALIGN_START   0   /* top / left */
#define UI_ALIGN_CENTER  1   /* centered */
#define UI_ALIGN_END     2   /* bottom / right */
#define UI_ALIGN_STRETCH 3   /* fill cross axis */

/* ═══ Main-axis justification ════════════════════════════════════ */

#define UI_JUST_START    0   /* pack to start */
#define UI_JUST_CENTER   1   /* center the pack */
#define UI_JUST_END      2   /* pack to end */
#define UI_JUST_BETWEEN  3   /* space-between */
#define UI_JUST_AROUND   4   /* space-around */

/* ═══ View event types ═══════════════════════════════════════════ */

#define UI_EV_NONE          0
#define UI_EV_CLICK         1
#define UI_EV_MOUSEDOWN     2
#define UI_EV_MOUSEUP       3
#define UI_EV_MOUSEMOVE     4
#define UI_EV_HOVER_ENTER   5
#define UI_EV_HOVER_EXIT    6
#define UI_EV_KEYDOWN       7
#define UI_EV_FOCUS         8
#define UI_EV_BLUR          9
#define UI_EV_SCROLL        10

/* ═══ Structs ════════════════════════════════════════════════════ */

/* How a view determines its own size */
typedef struct {
    uint8_t  w_mode;   /* UI_SIZE_* */
    uint8_t  h_mode;
    int16_t  w;        /* used when w_mode == UI_SIZE_FIXED */
    int16_t  h;        /* used when h_mode == UI_SIZE_FIXED */
    int16_t  flex;     /* flex-grow factor × 1000  (1000 = 1.0) */
                       /* only used when mode == UI_SIZE_FILL  */
} ui_size_t;

/* Flex layout properties for a container */
typedef struct {
    uint8_t  direction;    /* UI_DIR_* */
    uint8_t  align;        /* UI_ALIGN_* (cross-axis) */
    uint8_t  justify;      /* UI_JUST_*  (main-axis)  */
    int16_t  gap;          /* pixels between children */
    int16_t  pad_top;
    int16_t  pad_right;
    int16_t  pad_bottom;
    int16_t  pad_left;
} ui_layout_t;

/* Text alignment (matches ui_font.h constants) */
#define UI_TEXT_LEFT    0
#define UI_TEXT_CENTER  1
#define UI_TEXT_RIGHT   2

/* Visual style for one state */
typedef struct {
    uint32_t bg;           /* background RGB; 0 = transparent */
    uint32_t fg;           /* text/icon foreground RGB */
    uint32_t border_color; /* border RGB; 0 = no border */
    uint8_t  border_w;     /* border thickness in px */
    uint8_t  radius;       /* corner radius in px */
    uint8_t  opacity;      /* 0-255 (255 = opaque) */
    uint8_t  shadow;       /* TOK_SHADOW_* level */
    uint8_t  font_px;      /* text size in px (0 = default 13px) */
    uint8_t  text_align;   /* UI_TEXT_LEFT / CENTER / RIGHT */
} ui_style_t;

/* Forward declare for event type */
typedef struct ui_view ui_view_t;

/* Event passed to handlers and bubbled up the tree */
typedef struct {
    uint8_t      type;     /* UI_EV_* */
    uint8_t      stopped;  /* set via ui_view_event_stop() to cancel bubble */
    int          mx, my;   /* screen coordinates */
    int          key;      /* scancode (keyboard events) */
    uint8_t      btn;      /* mouse button: 1=left 2=right 3=mid */
    ui_view_t   *target;   /* deepest hit view (original target) */
} ui_view_event_t;

/* ═══ View node ══════════════════════════════════════════════════ */

struct ui_view {
    /* ── Identity ─────────────────────────────────────────────── */
    uint32_t      id;
    const char   *debug_name;

    /* ── Tree ──────────────────────────────────────────────────── */
    ui_view_t    *parent;
    ui_view_t    *children[UI_MAX_CHILDREN];
    int           child_count;

    /* ── Declared size / layout ────────────────────────────────── */
    ui_size_t     size;
    ui_layout_t   layout;

    /* ── Computed bounds (written by ui_layout_pass) ────────────── */
    int           ax, ay;   /* absolute screen position */
    int           aw, ah;   /* absolute width / height  */

    /* ── Style (base + pseudo-state overrides) ──────────────────── */
    ui_style_t    style;
    ui_style_t    style_hover;   /* applied when hovered */
    ui_style_t    style_active;  /* applied when pressed */
    ui_style_t    style_focus;   /* applied when focused */

    /* ── State flags ────────────────────────────────────────────── */
    uint8_t       visible;
    uint8_t       hovered;
    uint8_t       pressed;
    uint8_t       focused;
    uint8_t       dirty;         /* needs repaint */
    uint8_t       layout_dirty;  /* needs layout pass */
    uint8_t       clip;          /* 1 = clip children to own bounds */
    uint8_t       focusable;     /* 1 = can receive keyboard focus */

    /* ── Event callbacks ────────────────────────────────────────── */
    void (*on_click) (ui_view_t *v, int mx, int my, void *ctx);
    void (*on_hover) (ui_view_t *v, int enter,      void *ctx);
    void (*on_key)   (ui_view_t *v, int key,        void *ctx);
    void (*on_scroll)(ui_view_t *v, int dx, int dy, void *ctx);
    /* Called after background is drawn; draw custom content here  */
    void (*on_paint) (ui_view_t *v, gfx_surface_t *surf);
    void  *event_ctx;

    /* ── Optional compositor surface (owned by this view) ────────── */
    comp_surface_t *surf;

    /* ── Text content (optional, e.g. for labels/buttons) ──────── */
    const char *text;      /* static or widget-owned string; NULL = no text */

    /* ── Widget private data ────────────────────────────────────── */
    void  *userdata;
};

/* ═══ View pool API ══════════════════════════════════════════════ */

/* Initialise the view pool — call once at startup */
void        ui_view_init(void);

/* Allocate a view from the pool.  Returns NULL if exhausted. */
ui_view_t  *ui_view_create(void);

/* Free a view and recursively free all its children.
   Removes itself from parent's children array. */
void        ui_view_destroy(ui_view_t *v);

/* ═══ Tree manipulation ══════════════════════════════════════════ */

/* Append child to parent.  Returns 1 on success, 0 if full. */
int         ui_view_append(ui_view_t *parent, ui_view_t *child);

/* Remove child from parent (does not free). */
void        ui_view_remove(ui_view_t *parent, ui_view_t *child);

/* ═══ Convenience: create + configure in one call ════════════════ */

/* Create a flex container (direction, gap, padding) */
ui_view_t  *ui_view_make_row(int gap, int pad);
ui_view_t  *ui_view_make_col(int gap, int pad);

/* Mark dirty up to the root */
void        ui_view_mark_dirty(ui_view_t *v);

/* ═══ Hit testing ════════════════════════════════════════════════ */

/* Return the deepest visible view that contains (x, y).
   Returns NULL if no view is hit. */
ui_view_t  *ui_view_hit_test(ui_view_t *root, int x, int y);

/* ═══ Event dispatch ═════════════════════════════════════════════ */

/* Call on every mouse update.  Handles hover enter/exit, click,
   mousedown/mouseup, and bubbling automatically. */
void        ui_view_dispatch_mouse(ui_view_t *root,
                                   int mx, int my,
                                   int btn, int down);

/* Dispatch a key event to the currently focused view, bubbling up. */
void        ui_view_dispatch_key(ui_view_t *root, int key);

/* Stop event from bubbling further (call from within a handler) */
void        ui_view_event_stop(ui_view_event_t *ev);

/* ═══ Focus management ═══════════════════════════════════════════ */

void        ui_view_focus(ui_view_t *v);
void        ui_view_blur(ui_view_t *v);
ui_view_t  *ui_view_get_focused(void);

/* ═══ Rendering ══════════════════════════════════════════════════ */

/* Recursively paint the view tree into surf.
   Only paints views that are dirty (or force=1). */
void        ui_view_render(ui_view_t *root, gfx_surface_t *surf, int force);

/* Resolve active style (merges base + pseudo-state) */
ui_style_t  ui_view_active_style(const ui_view_t *v);

#endif /* _KERNEL_UI_VIEW_H */
