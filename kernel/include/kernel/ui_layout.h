/* ui_layout.h — UIKit flexbox layout engine
 *
 * Two-pass layout algorithm (borrowed from CSS Flexbox):
 *
 *   Pass 1 — measure (bottom-up):
 *     Each view computes its preferred size given its children.
 *     FIXED views report their declared size.
 *     HUG views sum (or max) their children's preferred sizes.
 *     FILL views report 0 (size unknown until parent runs pass 2).
 *
 *   Pass 2 — place (top-down):
 *     Starting from the root with known (x, y, w, h), each container
 *     distributes its space to children according to flex rules:
 *     - Subtract fixed/hug children from available space
 *     - Divide remainder among FILL children proportional to flex value
 *     - Apply justification offset along main axis
 *     - Apply cross-axis alignment per child
 *     Then recurse into each child with its final bounds.
 *
 * Usage:
 *   ui_layout_pass(root, 0, 0, screen_w, screen_h);
 *   // all view->ax, ay, aw, ah are now valid
 */

#ifndef _KERNEL_UI_LAYOUT_H
#define _KERNEL_UI_LAYOUT_H

#include <kernel/ui_view.h>

/* Run both passes.
 * root — top of the view subtree to lay out
 * x, y — position of root in screen coordinates
 * w, h — available width/height for root
 */
void ui_layout_pass(ui_view_t *root, int x, int y, int w, int h);

/* Pass 1: walk bottom-up, compute preferred sizes.
 * Stores result in view->aw, view->ah.
 * Call before ui_layout_place(). */
void ui_layout_measure(ui_view_t *v);

/* Pass 2: walk top-down, assign final positions and sizes.
 * Must be called after ui_layout_measure() with the same root. */
void ui_layout_place(ui_view_t *v, int x, int y, int w, int h);

#endif /* _KERNEL_UI_LAYOUT_H */
