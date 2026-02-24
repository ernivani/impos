/* ui_event.c — Phase 1 stub
 * Simplified event ring buffer. Full input system replaces this in Phase 5.
 */
#include <kernel/ui_event.h>
#include <kernel/wm.h>
#include <kernel/mouse.h>
#include <kernel/idt.h>
#include <string.h>

#define EV_BUF_SIZE 64
static ui_event_t ev_buf[EV_BUF_SIZE];
static int ev_head = 0, ev_tail = 0, ev_count = 0;

void ui_event_init(void) { ev_head = ev_tail = ev_count = 0; }

void ui_push_event(ui_event_t *ev) {
    if (ev_count >= EV_BUF_SIZE) return;
    ev_buf[ev_tail] = *ev;
    ev_tail = (ev_tail + 1) % EV_BUF_SIZE;
    ev_count++;
}

int ui_poll_event(ui_event_t *out) {
    if (ev_count == 0) return 0;
    *out = ev_buf[ev_head];
    ev_head = (ev_head + 1) % EV_BUF_SIZE;
    ev_count--;
    return 1;
}

int ui_event_pending(void) { return ev_count > 0; }

void ui_idle_handler(void) { wm_mouse_idle(); wm_flush_pending(); }
