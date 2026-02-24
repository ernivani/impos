/* login.c — Phase 1 stub
 * Will be rebuilt as a first-class app in Phase 4.
 */
#include <kernel/login.h>
#include <kernel/gfx.h>
#include <kernel/shell.h>

void login_show_splash(void) { /* no splash during rewrite */ }

void login_run_setup(void) { }

int login_run(void) {
    return shell_login();
}
