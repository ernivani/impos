#ifndef _KERNEL_RADIAL_H
#define _KERNEL_RADIAL_H

/* Radial launcher: circular app picker that opens on Space. */

/* Initialize (creates compositor surface). Call after compositor_init(). */
void radial_init(void);

/* Show/hide the radial launcher. */
void radial_show(void);
void radial_hide(void);

/* Returns 1 if radial is currently visible. */
int radial_visible(void);

/* Handle mouse input. Returns 1 if event was consumed.
   right_click: 1 if this is a right-button event. */
int radial_mouse(int mx, int my, int btn_down, int btn_up, int right_click);

/* Handle key input. ch is ASCII char (0 if non-printable).
   Returns 1 if event was consumed.
   Handles: Escape (close), Enter (launch), arrows (navigate),
            alphanumeric (close radial, open drawer with prefix). */
int radial_key(char ch, int scancode);

/* Repaint the radial surface (call after state changes). */
void radial_paint(void);

#endif
