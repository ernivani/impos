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
    TIME("gpu_comp_init done");

    while (1) {
        switch (state) {
        case STATE_SPLASH:
            TIME("state: SPLASH");
            login_show_splash();
            state = shell_needs_setup() ? STATE_SETUP : STATE_LOGIN;
            break;

        case STATE_SETUP:
            TIME("state: SETUP");
            login_run_setup();
            ui_shell_notify_login();
            state = STATE_DESKTOP;
            break;

        case STATE_LOGIN:
            TIME("state: LOGIN");
            login_run();
            ui_shell_notify_login();
            TIME("state: login done, entering DESKTOP");
            state = STATE_DESKTOP;
            break;

        case STATE_DESKTOP: {
            TIME("state: DESKTOP");
            int action = ui_shell_run();
            if (action == DESKTOP_ACTION_POWER)
                state = STATE_LOGIN;
            break;
        }
        }
    }
}
