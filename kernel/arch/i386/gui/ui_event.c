#include <kernel/ui_event.h>
#include <kernel/wm.h>
#include <kernel/mouse.h>
#include <kernel/desktop.h>
#include <kernel/idt.h>
#include <string.h>
#include <stdio.h>

#define EVENT_QUEUE_SIZE 16

static ui_event_t event_queue[EVENT_QUEUE_SIZE];
static int eq_head, eq_tail, eq_count;

/* Mouse button state for edge detection */
static uint8_t prev_mouse_buttons;

void ui_event_init(void) {
    eq_head = 0;
    eq_tail = 0;
    eq_count = 0;
    prev_mouse_buttons = 0;
}

void ui_push_event(ui_event_t *ev) {
    if (eq_count >= EVENT_QUEUE_SIZE) return;
    event_queue[eq_tail] = *ev;
    eq_tail = (eq_tail + 1) % EVENT_QUEUE_SIZE;
    eq_count++;
}

static int pop_event(ui_event_t *out) {
    if (eq_count == 0) return 0;
    *out = event_queue[eq_head];
    eq_head = (eq_head + 1) % EVENT_QUEUE_SIZE;
    eq_count--;
    return 1;
}

int ui_event_pending(void) {
    return eq_count > 0;
}

int ui_poll_event(ui_event_t *out) {
    /* Check queued events first */
    if (pop_event(out)) return 1;

    /* Block on getchar (idle callback will push mouse/dock events) */
    char c = getchar();

    /* Check if idle callback pushed events */
    if (pop_event(out)) return 1;

    /* Check close request */
    if (wm_close_was_requested()) {
        wm_clear_close_request();
        out->type = UI_EVENT_CLOSE;
        return 1;
    }

    /* Check dock action */
    int da = wm_get_dock_action();
    if (da) {
        wm_clear_dock_action();
        out->type = UI_EVENT_DOCK;
        out->dock.action = da;
        return 1;
    }

    /* Wrap key press with modifier state */
    out->type = UI_EVENT_KEY_PRESS;
    out->key.key = c;
    out->key.ctrl = keyboard_get_ctrl();
    out->key.shift = keyboard_get_shift();
    out->key.alt = keyboard_get_alt();
    return 1;
}

/* Standard idle handler: processes mouse via WM and pushes events */
void ui_idle_handler(void) {
    wm_mouse_idle();
    wm_flush_pending();

    /* Check for close request */
    if (wm_close_was_requested()) {
        wm_clear_close_request();
        ui_event_t ev;
        ev.type = UI_EVENT_CLOSE;
        ui_push_event(&ev);
        keyboard_request_force_exit();
        return;
    }

    /* Check for dock action */
    int da = wm_get_dock_action();
    if (da) {
        /* Don't clear — let the app loop read it */
        ui_event_t ev;
        ev.type = UI_EVENT_DOCK;
        ev.dock.action = da;
        ui_push_event(&ev);
        keyboard_request_force_exit();
        return;
    }

    /* Detect mouse button transitions */
    uint8_t btns = mouse_get_buttons();
    int mx = mouse_get_x(), my = mouse_get_y();

    if ((btns & MOUSE_BTN_LEFT) && !(prev_mouse_buttons & MOUSE_BTN_LEFT)) {
        ui_event_t ev;
        ev.type = UI_EVENT_MOUSE_DOWN;
        ev.mouse.x = mx;
        ev.mouse.y = my;
        ev.mouse.wx = 0;
        ev.mouse.wy = 0;
        ev.mouse.buttons = btns;
        ui_push_event(&ev);
        keyboard_request_force_exit();
    }
    if (!(btns & MOUSE_BTN_LEFT) && (prev_mouse_buttons & MOUSE_BTN_LEFT)) {
        ui_event_t ev;
        ev.type = UI_EVENT_MOUSE_UP;
        ev.mouse.x = mx;
        ev.mouse.y = my;
        ev.mouse.wx = 0;
        ev.mouse.wy = 0;
        ev.mouse.buttons = btns;
        ui_push_event(&ev);
        keyboard_request_force_exit();
    }

    /* Mouse drag while left button held */
    if ((btns & MOUSE_BTN_LEFT) && (prev_mouse_buttons & MOUSE_BTN_LEFT)) {
        static int prev_mx, prev_my;
        if (mx != prev_mx || my != prev_my) {
            ui_event_t ev;
            ev.type = UI_EVENT_MOUSE_MOVE;
            ev.mouse.x = mx;
            ev.mouse.y = my;
            ev.mouse.wx = 0;
            ev.mouse.wy = 0;
            ev.mouse.buttons = btns;
            ui_push_event(&ev);
            keyboard_request_force_exit();
            prev_mx = mx;
            prev_my = my;
        }
    }

    prev_mouse_buttons = btns;
}
