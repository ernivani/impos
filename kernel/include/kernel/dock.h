#ifndef _KERNEL_DOCK_H
#define _KERNEL_DOCK_H

#define DOCK_ICON_SIZE   40
#define DOCK_PADDING     10
#define DOCK_GAP         12  /* gap from screen bottom */
#define DOCK_ITEM_GAP     8  /* gap between icons */

/* Create dock surface on COMP_LAYER_OVERLAY. Call after compositor_init(). */
void dock_init(void);

/* Repaint the dock (call on hover change, launch state change). */
void dock_paint(void);

/* Handle mouse event. Returns 1 if consumed (click inside dock). */
int  dock_mouse(int mx, int my, int down, int up);

/* Returns the dock action if an icon was clicked, or -1. Clears after read. */
int  dock_consume_action(void);

#endif
