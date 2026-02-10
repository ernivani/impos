#ifndef _KERNEL_STATE_H
#define _KERNEL_STATE_H

typedef enum {
    STATE_SPLASH,
    STATE_SETUP,
    STATE_LOGIN,
    STATE_DESKTOP
} os_state_t;

/* Main state machine loop — never returns */
void state_run(void);

#endif
