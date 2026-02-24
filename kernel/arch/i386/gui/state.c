#include <kernel/state.h>
#include <kernel/login.h>
#include <kernel/desktop.h>
#include <kernel/shell.h>
#include <kernel/io.h>

void state_run(void) {
    os_state_t state = STATE_SPLASH;

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
            desktop_notify_login();
            state = STATE_DESKTOP;
            break;

        case STATE_LOGIN:
            DBG("state: login_run");
            login_run();
            DBG("state: login done, notify");
            desktop_notify_login();
            DBG("state: desktop notify done");
            state = STATE_DESKTOP;
            break;

        case STATE_DESKTOP: {
            DBG("state: desktop_run");
            int action = desktop_run();
            DBG("state: desktop_run returned");
            if (action == DESKTOP_ACTION_POWER)
                state = STATE_LOGIN;
            break;
        }
        }
    }
}
