#ifndef _KERNEL_TERMINAL_APP_H
#define _KERNEL_TERMINAL_APP_H

/* Open the terminal window (or bring to front). */
void app_terminal_open(void);

/* Handle a single keypress routed from the main event loop.
   Returns 1 if the key was consumed. */
int terminal_app_handle_key(char c);

/* Per-frame tick: handles close button, canvas resize, fg app ticks.
   Call from ui_shell_run(). Also handles mouse clicks.
   Returns 1 if a click in the content area was consumed. */
int terminal_app_tick(int mx, int my, int btn_down, int btn_up);

/* Returns 1 if the terminal window is currently open. */
int terminal_app_win_open(void);

/* Returns the ui_window id of the terminal, or -1. */
int terminal_app_win_id(void);

#endif
