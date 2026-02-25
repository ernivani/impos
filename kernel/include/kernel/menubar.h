#ifndef _KERNEL_MENUBAR_H
#define _KERNEL_MENUBAR_H

#define MENUBAR_HEIGHT 24

/* Create menubar surface on COMP_LAYER_OVERLAY. Call after compositor_init(). */
void menubar_init(void);

/* Repaint the menubar (call once per second or on focus change). */
void menubar_paint(void);

/* Returns 1 if the click was inside the menubar and consumed. */
int  menubar_mouse(int mx, int my, int down);

#endif
