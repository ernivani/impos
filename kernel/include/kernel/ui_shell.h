/* ui_shell.h — UIKit desktop shell (Phase 4)
 *
 * Drop-in-compatible replacement for desktop.c.
 * Reuses the same DESKTOP_ACTION_* return codes so kernel_main.c
 * needs only a one-line change in Phase 5:
 *   s/desktop_init()/ui_shell_init()/
 *   s/desktop_run()/ui_shell_run()/
 *
 * Key changes vs desktop.c:
 *  • Uses ui_window_* instead of wm2_*
 *  • Demo window uses ui_window_canvas (gfx_surface_t, not raw ptr)
 *  • Context-menu window detection uses ui_window_info (not wm2_info_t)
 *  • All other behaviour is identical
 */

#ifndef _KERNEL_UI_SHELL_H
#define _KERNEL_UI_SHELL_H

#include <kernel/desktop.h>   /* DESKTOP_ACTION_* constants */

void ui_shell_init(void);
int  ui_shell_run(void);
void ui_shell_notify_login(void);  /* call instead of desktop_notify_login() */

#endif /* _KERNEL_UI_SHELL_H */
