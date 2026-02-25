#ifndef _KERNEL_MENUBAR_H
#define _KERNEL_MENUBAR_H

#define MENUBAR_HEIGHT 28

/* Create menubar surface on COMP_LAYER_OVERLAY. Call after compositor_init(). */
void menubar_init(void);

/* Repaint the menubar (call once per second, on focus change, or window open/close). */
void menubar_paint(void);

/* Returns 1 if the click was inside the menubar and consumed.
   right_click: 1 for right-button event. */
int menubar_mouse(int mx, int my, int btn_down, int btn_up, int right_click);

/* Call after any window open/close/minimize/restore to update pills. */
void menubar_update_windows(void);

#endif
