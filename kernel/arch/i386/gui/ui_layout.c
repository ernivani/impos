/* ui_layout.c — UIKit flexbox layout engine
 *
 * Two-pass algorithm:
 *   Pass 1 (measure): Walk bottom-up, compute preferred size.
 *                     Result stored in v->aw, v->ah.
 *   Pass 2 (place):   Walk top-down with known available space.
 *                     Sets v->ax, v->ay, v->aw, v->ah to final bounds.
 *
 * Integer arithmetic only — no floating point.
 * Flex grow values are × 1000 (1000 = 1.0).
 */

#include <kernel/ui_layout.h>
#include <kernel/ui_view.h>

/* ── Helpers ─────────────────────────────────────────────────── */

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* ── Pass 1: measure (bottom-up) ─────────────────────────────── */

void ui_layout_measure(ui_view_t *v)
{
    if (!v) return;

    /* Fixed: size is what was declared */
    if (v->size.w_mode == UI_SIZE_FIXED && v->size.h_mode == UI_SIZE_FIXED) {
        v->aw = v->size.w;
        v->ah = v->size.h;
        /* Still measure children so they have correct preferred sizes */
        for (int i = 0; i < v->child_count; i++)
            ui_layout_measure(v->children[i]);
        return;
    }

    /* Measure children first (they may be needed for HUG sizing) */
    for (int i = 0; i < v->child_count; i++)
        ui_layout_measure(v->children[i]);

    /* Fill: preferred = 0 (parent will expand in pass 2) */
    if (v->size.w_mode == UI_SIZE_FILL && v->size.h_mode == UI_SIZE_FILL) {
        v->aw = 0;
        v->ah = 0;
        return;
    }

    /* HUG: derive preferred size from children */
    int ph = v->layout.pad_top + v->layout.pad_bottom;
    int pv = v->layout.pad_left + v->layout.pad_right;

    if (v->layout.direction == UI_DIR_ROW) {
        /* Main axis = width:  sum widths + gaps */
        /* Cross axis = height: max height */
        int total_w = pv;
        int max_h   = 0;
        for (int i = 0; i < v->child_count; i++) {
            ui_view_t *c = v->children[i];
            if (!c->visible) continue;
            total_w += c->aw;
            if (c->ah > max_h) max_h = c->ah;
        }
        /* Add gaps between visible children */
        int visible = 0;
        for (int i = 0; i < v->child_count; i++)
            if (v->children[i]->visible) visible++;
        if (visible > 1) total_w += v->layout.gap * (visible - 1);

        v->aw = (v->size.w_mode == UI_SIZE_FIXED) ? v->size.w : total_w;
        v->ah = (v->size.h_mode == UI_SIZE_FIXED) ? v->size.h : (max_h + ph);
    } else {
        /* UI_DIR_COL */
        int total_h = ph;
        int max_w   = 0;
        for (int i = 0; i < v->child_count; i++) {
            ui_view_t *c = v->children[i];
            if (!c->visible) continue;
            total_h += c->ah;
            if (c->aw > max_w) max_w = c->aw;
        }
        int visible = 0;
        for (int i = 0; i < v->child_count; i++)
            if (v->children[i]->visible) visible++;
        if (visible > 1) total_h += v->layout.gap * (visible - 1);

        v->aw = (v->size.w_mode == UI_SIZE_FIXED) ? v->size.w : (max_w + pv);
        v->ah = (v->size.h_mode == UI_SIZE_FIXED) ? v->size.h : total_h;
    }

    /* Mixed fixed/fill/hug: apply fixed overrides */
    if (v->size.w_mode == UI_SIZE_FIXED) v->aw = v->size.w;
    if (v->size.h_mode == UI_SIZE_FIXED) v->ah = v->size.h;
}

/* ── Pass 2: place (top-down) ────────────────────────────────── */

void ui_layout_place(ui_view_t *v, int x, int y, int w, int h)
{
    if (!v) return;

    /* Assign final position and size */
    v->ax = x;
    v->ay = y;
    v->aw = w;
    v->ah = h;

    if (v->child_count == 0) return;

    /* Inner content area after padding */
    int ix = x + v->layout.pad_left;
    int iy = y + v->layout.pad_top;
    int iw = w - v->layout.pad_left - v->layout.pad_right;
    int ih = h - v->layout.pad_top  - v->layout.pad_bottom;
    if (iw < 0) iw = 0;
    if (ih < 0) ih = 0;

    /* Count visible children */
    int visible = 0;
    for (int i = 0; i < v->child_count; i++)
        if (v->children[i]->visible) visible++;
    if (visible == 0) return;

    int gap = v->layout.gap;

    if (v->layout.direction == UI_DIR_ROW) {
        /* ── ROW layout ────────────────────────────────────────── */

        /* Step 1: compute base sizes for non-FILL children */
        int fixed_total = 0;
        int fill_flex   = 0;
        int fill_count  = 0;

        for (int i = 0; i < v->child_count; i++) {
            ui_view_t *c = v->children[i];
            if (!c->visible) continue;
            if (c->size.w_mode == UI_SIZE_FILL) {
                fill_flex += (c->size.flex > 0 ? c->size.flex : 1000);
                fill_count++;
            } else {
                fixed_total += c->aw;
            }
        }

        /* Gap budget */
        int gap_total = gap * (visible - 1);
        int remaining = iw - fixed_total - gap_total;
        if (remaining < 0) remaining = 0;

        /* Step 2: compute main-axis start cursor based on justification */
        int cursor;
        int extra_gap = 0;
        int between_extra = 0;

        if (fill_count == 0) {
            /* No FILL children: justification applies */
            int content_w = fixed_total + gap_total;
            switch (v->layout.justify) {
            case UI_JUST_CENTER:
                cursor = ix + (iw - content_w) / 2;
                break;
            case UI_JUST_END:
                cursor = ix + iw - content_w;
                break;
            case UI_JUST_BETWEEN:
                cursor = ix;
                if (visible > 1)
                    between_extra = (iw - fixed_total) / (visible - 1);
                break;
            case UI_JUST_AROUND:
                cursor = ix;
                if (visible > 0) {
                    extra_gap = (iw - fixed_total) / visible;
                    cursor = ix + extra_gap / 2;
                    between_extra = extra_gap;
                }
                break;
            default: /* UI_JUST_START */
                cursor = ix;
                break;
            }
        } else {
            cursor = ix;
        }
        if (cursor < ix) cursor = ix;

        /* Step 3: place children */
        int fill_index = 0;
        for (int i = 0; i < v->child_count; i++) {
            ui_view_t *c = v->children[i];
            if (!c->visible) continue;

            /* Child width */
            int cw;
            if (c->size.w_mode == UI_SIZE_FILL) {
                int my_flex = (c->size.flex > 0 ? c->size.flex : 1000);
                /* Distribute remaining proportionally, rounding down */
                cw = (fill_flex > 0) ? (remaining * my_flex / fill_flex) : 0;
                fill_index++;
                /* Give leftover pixels to last fill child */
                if (fill_index == fill_count)
                    cw = remaining - (fill_index - 1) * (fill_flex > 0 ? remaining / fill_flex * 1000 / fill_flex : 0);
                if (cw < 0) cw = 0;
            } else {
                cw = c->aw;
            }

            /* Child height + cross-axis alignment */
            int ch, cy;
            switch (v->layout.align) {
            case UI_ALIGN_STRETCH:
                ch = ih;
                cy = iy;
                break;
            case UI_ALIGN_CENTER:
                ch = (c->size.h_mode == UI_SIZE_FILL) ? ih : c->ah;
                cy = iy + (ih - ch) / 2;
                break;
            case UI_ALIGN_END:
                ch = (c->size.h_mode == UI_SIZE_FILL) ? ih : c->ah;
                cy = iy + ih - ch;
                break;
            default: /* UI_ALIGN_START */
                ch = (c->size.h_mode == UI_SIZE_FILL) ? ih : c->ah;
                cy = iy;
                break;
            }
            if (ch < 0) ch = 0;
            if (cy < iy) cy = iy;

            ui_layout_place(c, cursor, cy, cw, ch);
            cursor += cw;

            /* Add gap (not after last child) */
            if (fill_count > 0 || between_extra == 0)
                cursor += gap;
            else
                cursor += between_extra;
        }

    } else {
        /* ── COL layout ────────────────────────────────────────── */

        int fixed_total = 0;
        int fill_flex   = 0;
        int fill_count  = 0;

        for (int i = 0; i < v->child_count; i++) {
            ui_view_t *c = v->children[i];
            if (!c->visible) continue;
            if (c->size.h_mode == UI_SIZE_FILL) {
                fill_flex += (c->size.flex > 0 ? c->size.flex : 1000);
                fill_count++;
            } else {
                fixed_total += c->ah;
            }
        }

        int gap_total = gap * (visible - 1);
        int remaining = ih - fixed_total - gap_total;
        if (remaining < 0) remaining = 0;

        int cursor;
        int between_extra = 0;
        int extra_gap = 0;

        if (fill_count == 0) {
            int content_h = fixed_total + gap_total;
            switch (v->layout.justify) {
            case UI_JUST_CENTER:
                cursor = iy + (ih - content_h) / 2;
                break;
            case UI_JUST_END:
                cursor = iy + ih - content_h;
                break;
            case UI_JUST_BETWEEN:
                cursor = iy;
                if (visible > 1)
                    between_extra = (ih - fixed_total) / (visible - 1);
                break;
            case UI_JUST_AROUND:
                cursor = iy;
                if (visible > 0) {
                    extra_gap = (ih - fixed_total) / visible;
                    cursor = iy + extra_gap / 2;
                    between_extra = extra_gap;
                }
                break;
            default:
                cursor = iy;
                break;
            }
        } else {
            cursor = iy;
        }
        if (cursor < iy) cursor = iy;

        int fill_index = 0;
        for (int i = 0; i < v->child_count; i++) {
            ui_view_t *c = v->children[i];
            if (!c->visible) continue;

            int ch;
            if (c->size.h_mode == UI_SIZE_FILL) {
                int my_flex = (c->size.flex > 0 ? c->size.flex : 1000);
                ch = (fill_flex > 0) ? (remaining * my_flex / fill_flex) : 0;
                fill_index++;
                if (ch < 0) ch = 0;
            } else {
                ch = c->ah;
            }

            int cw, cx;
            switch (v->layout.align) {
            case UI_ALIGN_STRETCH:
                cw = iw;
                cx = ix;
                break;
            case UI_ALIGN_CENTER:
                cw = (c->size.w_mode == UI_SIZE_FILL) ? iw : c->aw;
                cx = ix + (iw - cw) / 2;
                break;
            case UI_ALIGN_END:
                cw = (c->size.w_mode == UI_SIZE_FILL) ? iw : c->aw;
                cx = ix + iw - cw;
                break;
            default:
                cw = (c->size.w_mode == UI_SIZE_FILL) ? iw : c->aw;
                cx = ix;
                break;
            }
            if (cw < 0) cw = 0;
            if (cx < ix) cx = ix;

            ui_layout_place(c, cx, cursor, cw, ch);
            cursor += ch;

            if (fill_count > 0 || between_extra == 0)
                cursor += gap;
            else
                cursor += between_extra;
        }
    }

    /* Prevent compiler warning for unused clamp */
    (void)clamp;
}

/* ── Full layout pass ─────────────────────────────────────────── */

void ui_layout_pass(ui_view_t *root, int x, int y, int w, int h)
{
    if (!root) return;
    ui_layout_measure(root);
    ui_layout_place(root, x, y, w, h);
}
