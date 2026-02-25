#ifndef _KERNEL_DRAWER_H
#define _KERNEL_DRAWER_H

/* App drawer: full-screen overlay with search + category grid. */

/* Initialize (creates compositor surface). Call after compositor_init(). */
void drawer_init(void);

/* Show the app drawer, optionally with a prefilled search string. */
void drawer_show(const char *prefill);

/* Hide the app drawer. */
void drawer_hide(void);

/* Returns 1 if drawer is currently visible. */
int drawer_visible(void);

/* Handle mouse input. Returns 1 if event was consumed.
   right_click: 1 for right-button, 0 for left. */
int drawer_mouse(int mx, int my, int btn_down, int btn_up, int right_click);

/* Handle key input. ch is ASCII char.
   Returns 1 if event was consumed. */
int drawer_key(char ch, int scancode);

/* Repaint the drawer (call after search/scroll state changes). */
void drawer_paint(void);

/* Tick: drive open/close alpha animation. Call every frame. */
void drawer_tick(void);

#endif
