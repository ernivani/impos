/* ui_view.c — UIKit view tree, event dispatch, and rendering
 *
 * Implements:
 *   - Static pool of 256 view nodes (no per-view malloc)
 *   - Parent/child tree with append/remove
 *   - Recursive hit testing (deepest visible child wins)
 *   - Mouse event dispatch with hover-enter/exit tracking and bubbling
 *   - Keyboard event dispatch to focused view with bubbling
 *   - Global focus management
 *   - Recursive render pass: background → on_paint → children
 */

#include <kernel/ui_view.h>
#include <kernel/ui_layout.h>
#include <kernel/ui_font.h>
#include <kernel/ui_fx.h>
#include <kernel/gfx.h>
#include <string.h>
#include <stdlib.h>

/* ── Pool ────────────────────────────────────────────────────── */

static ui_view_t  pool[UI_VIEW_POOL_SIZE];
static uint8_t    pool_used[UI_VIEW_POOL_SIZE];
static uint32_t   next_id = 1;

void ui_view_init(void)
{
    memset(pool,      0, sizeof(pool));
    memset(pool_used, 0, sizeof(pool_used));
    next_id = 1;
}

ui_view_t *ui_view_create(void)
{
    for (int i = 0; i < UI_VIEW_POOL_SIZE; i++) {
        if (!pool_used[i]) {
            pool_used[i] = 1;
            ui_view_t *v = &pool[i];
            memset(v, 0, sizeof(*v));
            v->id      = next_id++;
            v->visible = 1;
            /* default style: transparent background, fully opaque */
            v->style.opacity       = 255;
            v->style_hover.opacity = 255;
            v->style_active.opacity= 255;
            v->style_focus.opacity = 255;
            return v;
        }
    }
    return NULL;  /* pool exhausted */
}

void ui_view_destroy(ui_view_t *v)
{
    if (!v) return;

    /* Recurse: destroy children first */
    for (int i = 0; i < v->child_count; i++) {
        ui_view_destroy(v->children[i]);
        v->children[i] = NULL;
    }
    v->child_count = 0;

    /* Remove from parent */
    if (v->parent)
        ui_view_remove(v->parent, v);

    /* If we own a compositor surface, release it */
    if (v->surf) {
        comp_surface_destroy(v->surf);
        v->surf = NULL;
    }

    /* Return to pool */
    int idx = (int)(v - pool);
    if (idx >= 0 && idx < UI_VIEW_POOL_SIZE)
        pool_used[idx] = 0;
}

/* ── Tree ────────────────────────────────────────────────────── */

int ui_view_append(ui_view_t *parent, ui_view_t *child)
{
    if (!parent || !child) return 0;
    if (parent->child_count >= UI_MAX_CHILDREN) return 0;
    if (child->parent) ui_view_remove(child->parent, child);

    child->parent = parent;
    parent->children[parent->child_count++] = child;
    parent->layout_dirty = 1;
    return 1;
}

void ui_view_remove(ui_view_t *parent, ui_view_t *child)
{
    if (!parent || !child) return;
    for (int i = 0; i < parent->child_count; i++) {
        if (parent->children[i] == child) {
            /* Shift remaining children down */
            for (int j = i; j < parent->child_count - 1; j++)
                parent->children[j] = parent->children[j + 1];
            parent->children[--parent->child_count] = NULL;
            child->parent = NULL;
            parent->layout_dirty = 1;
            return;
        }
    }
}

/* ── Convenience constructors ─────────────────────────────────── */

ui_view_t *ui_view_make_row(int gap, int pad)
{
    ui_view_t *v = ui_view_create();
    if (!v) return NULL;
    v->layout.direction = UI_DIR_ROW;
    v->layout.gap       = (int16_t)gap;
    v->layout.pad_top    = (int16_t)pad;
    v->layout.pad_right  = (int16_t)pad;
    v->layout.pad_bottom = (int16_t)pad;
    v->layout.pad_left   = (int16_t)pad;
    v->size.w_mode = UI_SIZE_HUG;
    v->size.h_mode = UI_SIZE_HUG;
    return v;
}

ui_view_t *ui_view_make_col(int gap, int pad)
{
    ui_view_t *v = ui_view_create();
    if (!v) return NULL;
    v->layout.direction = UI_DIR_COL;
    v->layout.gap       = (int16_t)gap;
    v->layout.pad_top    = (int16_t)pad;
    v->layout.pad_right  = (int16_t)pad;
    v->layout.pad_bottom = (int16_t)pad;
    v->layout.pad_left   = (int16_t)pad;
    v->size.w_mode = UI_SIZE_HUG;
    v->size.h_mode = UI_SIZE_HUG;
    return v;
}

void ui_view_mark_dirty(ui_view_t *v)
{
    ui_view_t *cur = v;
    while (cur) {
        cur->dirty = 1;
        cur = cur->parent;
    }
}

/* ── Hit testing ─────────────────────────────────────────────── */

ui_view_t *ui_view_hit_test(ui_view_t *root, int x, int y)
{
    if (!root || !root->visible) return NULL;
    /* Check bounds */
    if (x < root->ax || x >= root->ax + root->aw) return NULL;
    if (y < root->ay || y >= root->ay + root->ah) return NULL;

    /* Test children in reverse order (last child = top-most) */
    for (int i = root->child_count - 1; i >= 0; i--) {
        ui_view_t *hit = ui_view_hit_test(root->children[i], x, y);
        if (hit) return hit;
    }

    /* If clipping is on and point is in bounds, this view is the hit */
    return root;
}

/* ── Focus ───────────────────────────────────────────────────── */

static ui_view_t *focused_view = NULL;

void ui_view_focus(ui_view_t *v)
{
    if (focused_view == v) return;

    /* Blur old */
    if (focused_view) {
        focused_view->focused = 0;
        ui_view_mark_dirty(focused_view);
        if (focused_view->on_key) {
            /* fire blur — we reuse on_key with key=-1 as blur signal;
               proper UI_EV_BLUR is dispatched via the event path */
        }
    }

    focused_view = v;
    if (v) {
        v->focused = 1;
        ui_view_mark_dirty(v);
    }
}

void ui_view_blur(ui_view_t *v)
{
    if (focused_view == v) {
        if (v) { v->focused = 0; ui_view_mark_dirty(v); }
        focused_view = NULL;
    }
}

ui_view_t *ui_view_get_focused(void)
{
    return focused_view;
}

/* ── Event helpers ───────────────────────────────────────────── */

void ui_view_event_stop(ui_view_event_t *ev)
{
    if (ev) ev->stopped = 1;
}

/* Fire an event on v and bubble up through parents until stopped */
static void fire_and_bubble(ui_view_t *v, ui_view_event_t *ev)
{
    ui_view_t *cur = v;
    while (cur && !ev->stopped) {
        switch (ev->type) {
        case UI_EV_CLICK:
            if (cur->on_click)
                cur->on_click(cur, ev->mx, ev->my, cur->event_ctx);
            break;
        case UI_EV_HOVER_ENTER:
        case UI_EV_HOVER_EXIT:
            if (cur->on_hover)
                cur->on_hover(cur, ev->type == UI_EV_HOVER_ENTER ? 1 : 0,
                              cur->event_ctx);
            break;
        case UI_EV_KEYDOWN:
            if (cur->on_key)
                cur->on_key(cur, ev->key, cur->event_ctx);
            break;
        case UI_EV_SCROLL:
            if (cur->on_scroll)
                cur->on_scroll(cur, ev->mx, ev->my, cur->event_ctx);
            break;
        }
        cur = cur->parent;
    }
}

/* ── Mouse dispatch ──────────────────────────────────────────── */

static ui_view_t *prev_hovered = NULL;   /* last frame's hovered view */
static ui_view_t *press_target = NULL;   /* view that received mousedown */

void ui_view_dispatch_mouse(ui_view_t *root, int mx, int my,
                            int btn, int down)
{
    ui_view_t *hit = ui_view_hit_test(root, mx, my);

    /* ── Hover enter / exit ─────────────────────────────────────── */
    if (hit != prev_hovered) {
        if (prev_hovered) {
            prev_hovered->hovered = 0;
            ui_view_mark_dirty(prev_hovered);
            ui_view_event_t ev = {0};
            ev.type = UI_EV_HOVER_EXIT;
            ev.mx = mx; ev.my = my;
            ev.target = prev_hovered;
            fire_and_bubble(prev_hovered, &ev);
        }
        if (hit) {
            hit->hovered = 1;
            ui_view_mark_dirty(hit);
            ui_view_event_t ev = {0};
            ev.type = UI_EV_HOVER_ENTER;
            ev.mx = mx; ev.my = my;
            ev.target = hit;
            fire_and_bubble(hit, &ev);
        }
        prev_hovered = hit;
    }

    /* ── Button events ──────────────────────────────────────────── */
    if (btn) {
        if (down) {
            /* Mouse down */
            press_target = hit;
            if (hit) {
                hit->pressed = 1;
                ui_view_mark_dirty(hit);
                if (hit->focusable)
                    ui_view_focus(hit);
                ui_view_event_t ev = {0};
                ev.type = UI_EV_MOUSEDOWN;
                ev.mx = mx; ev.my = my; ev.btn = (uint8_t)btn;
                ev.target = hit;
                fire_and_bubble(hit, &ev);
            }
        } else {
            /* Mouse up */
            if (press_target) {
                press_target->pressed = 0;
                ui_view_mark_dirty(press_target);
            }

            if (hit) {
                ui_view_event_t ev = {0};
                ev.type = UI_EV_MOUSEUP;
                ev.mx = mx; ev.my = my; ev.btn = (uint8_t)btn;
                ev.target = hit;
                fire_and_bubble(hit, &ev);

                /* Click = down + up on same view */
                if (hit == press_target) {
                    ev.type = UI_EV_CLICK;
                    ev.stopped = 0;
                    fire_and_bubble(hit, &ev);
                }
            }
            press_target = NULL;
        }
    }
}

/* ── Keyboard dispatch ───────────────────────────────────────── */

void ui_view_dispatch_key(ui_view_t *root, int key)
{
    (void)root;
    ui_view_t *target = focused_view;
    if (!target) return;

    ui_view_event_t ev = {0};
    ev.type   = UI_EV_KEYDOWN;
    ev.key    = key;
    ev.target = target;
    fire_and_bubble(target, &ev);
}

/* ── Style resolution ────────────────────────────────────────── */

ui_style_t ui_view_active_style(const ui_view_t *v)
{
    ui_style_t s = v->style;

    if (v->pressed && v->style_active.opacity) {
        /* Merge: override non-zero fields from active style */
        if (v->style_active.bg)           s.bg           = v->style_active.bg;
        if (v->style_active.fg)           s.fg           = v->style_active.fg;
        if (v->style_active.border_color) s.border_color = v->style_active.border_color;
        if (v->style_active.radius)       s.radius       = v->style_active.radius;
        s.opacity = v->style_active.opacity;
    } else if (v->focused && v->style_focus.opacity) {
        if (v->style_focus.bg)           s.bg           = v->style_focus.bg;
        if (v->style_focus.fg)           s.fg           = v->style_focus.fg;
        if (v->style_focus.border_color) s.border_color = v->style_focus.border_color;
        if (v->style_focus.border_w)     s.border_w     = v->style_focus.border_w;
        if (v->style_focus.radius)       s.radius       = v->style_focus.radius;
        s.opacity = v->style_focus.opacity;
    } else if (v->hovered && v->style_hover.opacity) {
        if (v->style_hover.bg)           s.bg           = v->style_hover.bg;
        if (v->style_hover.fg)           s.fg           = v->style_hover.fg;
        if (v->style_hover.border_color) s.border_color = v->style_hover.border_color;
        if (v->style_hover.radius)       s.radius       = v->style_hover.radius;
        s.opacity = v->style_hover.opacity;
    }

    return s;
}

/* ── Rendering ───────────────────────────────────────────────── */

void ui_view_render(ui_view_t *root, gfx_surface_t *surf, int force)
{
    if (!root || !root->visible) return;
    if (!force && !root->dirty)  return;

    ui_style_t s = ui_view_active_style(root);

    int x = root->ax, y = root->ay;
    int w = root->aw, h = root->ah;

    /* ── Shadow (draw before background so it sits beneath) ─────── */
    if (s.shadow > 0 && w > 0 && h > 0)
        ui_fx_draw_shadow(surf, x, y, w, h, s.radius, (int)s.shadow);

    /* ── Background ─────────────────────────────────────────────── */
    if (s.bg && w > 0 && h > 0) {
        uint8_t alpha = s.opacity;
        if (s.radius > 0) {
            if (alpha < 255)
                gfx_surf_rounded_rect_alpha(surf, x, y, w, h,
                                            s.radius, s.bg, alpha);
            else
                gfx_surf_rounded_rect(surf, x, y, w, h, s.radius, s.bg);
        } else {
            if (alpha < 255)
                gfx_surf_fill_rect_alpha(surf, x, y, w, h, s.bg, alpha);
            else
                gfx_surf_fill_rect(surf, x, y, w, h, s.bg);
        }
    }

    /* ── Border ──────────────────────────────────────────────────── */
    if (s.border_color && s.border_w > 0) {
        if (s.radius > 0)
            gfx_surf_rounded_rect_outline(surf, x, y, w, h,
                                          s.radius, s.border_color);
        else
            gfx_surf_draw_rect(surf, x, y, w, h, s.border_color);
    }

    /* ── Custom paint ────────────────────────────────────────────── */
    if (root->on_paint)
        root->on_paint(root, surf);

    /* ── Text content ────────────────────────────────────────────── */
    if (root->text && root->text[0] && s.fg && w > 0 && h > 0) {
        int tpx = s.font_px ? (int)s.font_px : 13;
        ui_font_draw_in_rect(surf, x, y, w, h, root->text, s.fg,
                             tpx, (int)s.text_align);
    }

    /* ── Recurse children ────────────────────────────────────────── */
    for (int i = 0; i < root->child_count; i++)
        ui_view_render(root->children[i], surf, force);

    root->dirty = 0;
}
