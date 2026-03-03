/* login.c — Login & first-boot setup for GUI mode */
#include <kernel/login.h>
#include <kernel/gfx.h>
#include <kernel/shell.h>
#include <kernel/user.h>
#include <kernel/hostname.h>
#include <kernel/fs.h>

void login_show_splash(void) { /* no splash during rewrite */ }

void login_run_setup(void) {
    /* First boot in GUI mode: auto-create users with default passwords.
     * Terminal mode has an interactive wizard in shell_initialize(); here
     * we bootstrap so the desktop can start. Matches shell wizard conventions. */
    hostname_set("imposos");
    hostname_save();

    /* Root user — home at /home/root (created by fs_initialize) */
    user_create("root", "root", "/home/root", 0, 0);
    user_create_home_dirs("/home/root");

    /* Default regular user */
    fs_create_file("/home/user", 1);
    user_create("user", "user", "/home/user", 1000, 1000);
    user_create_home_dirs("/home/user");

    user_save();

    /* Log in as user (not root, for safety) */
    user_set_current("user");
    fs_change_directory("/home/user");
}

int login_run(void) {
    return shell_login();
}
