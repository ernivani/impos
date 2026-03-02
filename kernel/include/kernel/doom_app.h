/* doom_app.h — DOOM windowed mode interface.
 *
 * Wraps the doomgeneric port into a cooperative windowed app that
 * renders to a ui_window canvas and ticks from the compositor loop.
 */
#ifndef _KERNEL_DOOM_APP_H
#define _KERNEL_DOOM_APP_H

/* Open the DOOM window (or raise it if already open).
 * Initializes DOOM on first call (doomgeneric_Create). */
void doom_app_open(void);

/* Per-frame tick: runs one doomgeneric_Tick() with setjmp protection,
 * blits DOOM's 320x200 output to the window canvas. */
int doom_app_tick(int mx, int my, int btn_down, int btn_up);

/* Query state. */
int doom_app_win_open(void);
int doom_app_win_id(void);

#endif /* _KERNEL_DOOM_APP_H */
