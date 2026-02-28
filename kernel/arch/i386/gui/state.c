#include <kernel/state.h>
#include <kernel/login.h>
#include <kernel/ui_shell.h>   /* Phase 5: replaces desktop_* calls */
#include <kernel/desktop.h>    /* DESKTOP_ACTION_* constants + stubs */
#include <kernel/shell.h>
#include <kernel/io.h>
#include <kernel/gpu_compositor.h>  /* GPU_DEBUG: self-test */

void state_run(void) {
    os_state_t state = STATE_SPLASH;

    /* GPU_DEBUG: run GPU compositor self-test before login blocks */
    DBG("state: GPU_DEBUG self-test before login");
    gpu_comp_init();
    DBG("state: GPU_DEBUG self-test done");

    while (1) {
        switch (state) {
        case STATE_SPLASH:
            DBG("state: login_show_splash");
            login_show_splash();
            DBG("state: splash done, checking setup");
            state = shell_needs_setup() ? STATE_SETUP : STATE_LOGIN;
            DBG("state: transition ok");
            break;

        case STATE_SETUP:
            DBG("state: login_run_setup");
            login_run_setup();
            DBG("state: setup done");
            ui_shell_notify_login();
            state = STATE_DESKTOP;
            break;

        case STATE_LOGIN:
            DBG("state: login_run");
            login_run();
            DBG("state: login done, notify");
            ui_shell_notify_login();
            DBG("state: desktop notify done");
            state = STATE_DESKTOP;
            break;

        case STATE_DESKTOP: {
            DBG("state: ui_shell_run");
            int action = ui_shell_run();
            DBG("state: ui_shell_run returned");
            if (action == DESKTOP_ACTION_POWER)
                state = STATE_LOGIN;
            break;
        }
        }
    }
}
