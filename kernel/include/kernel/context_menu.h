#ifndef _KERNEL_CONTEXT_MENU_H
#define _KERNEL_CONTEXT_MENU_H

/* Context menu: right-click popup on desktop. */

/* Initialize (call after compositor_init()). */
void ctx_menu_init(void);

/* Show context menu at screen position (x, y).
   Clamps to screen edges automatically. */
void ctx_menu_show(int x, int y);

/* Hide the context menu. */
void ctx_menu_hide(void);

/* Returns 1 if context menu is currently visible. */
int ctx_menu_visible(void);

/* Handle mouse input. Returns 1 if event was consumed. */
int ctx_menu_mouse(int mx, int my, int btn_down, int btn_up);

/* Repaint (call after hover changes). */
void ctx_menu_paint(void);

#endif
