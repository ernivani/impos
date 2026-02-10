#include <kernel/state.h>
#include <kernel/login.h>
#include <kernel/desktop.h>
#include <kernel/shell.h>

void state_run(void) {
    os_state_t state = STATE_SPLASH;

    while (1) {
        switch (state) {
        case STATE_SPLASH:
            login_show_splash();
            state = shell_needs_setup() ? STATE_SETUP : STATE_LOGIN;
            break;

        case STATE_SETUP:
            login_run_setup();
            /* User is already authenticated after setup */
            desktop_notify_login();
            state = STATE_DESKTOP;
            break;

        case STATE_LOGIN:
            login_run();
            desktop_notify_login();
            state = STATE_DESKTOP;
            break;

        case STATE_DESKTOP: {
            int action = desktop_run();
            if (action == DESKTOP_ACTION_POWER)
                state = STATE_LOGIN;
            break;
        }
        }
    }
}
