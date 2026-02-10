#ifndef _KERNEL_UI_EVENT_H
#define _KERNEL_UI_EVENT_H

#include <stdint.h>

/* Event types */
#define UI_EVENT_NONE       0
#define UI_EVENT_KEY_PRESS  1
#define UI_EVENT_MOUSE_MOVE 2
#define UI_EVENT_MOUSE_DOWN 3
#define UI_EVENT_MOUSE_UP   4
#define UI_EVENT_CLOSE      5
#define UI_EVENT_DOCK       6
#define UI_EVENT_RESIZE     7
#define UI_EVENT_FOCUS      8
#define UI_EVENT_BLUR       9

typedef struct {
    int type;
    union {
        struct {
            char key;
            uint8_t ctrl;
            uint8_t alt;
            uint8_t shift;
        } key;
        struct {
            int x, y;       /* screen coordinates */
            int wx, wy;     /* window-relative */
            uint8_t buttons;
        } mouse;
        struct {
            int action;     /* DESKTOP_ACTION_* */
        } dock;
    };
} ui_event_t;

void ui_event_init(void);
void ui_push_event(ui_event_t *ev);
int  ui_poll_event(ui_event_t *out);
int  ui_event_pending(void);

/* Standard idle handler for apps using the event system */
void ui_idle_handler(void);

#endif
