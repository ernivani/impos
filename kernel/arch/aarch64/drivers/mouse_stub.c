/*
 * Mouse Stub — aarch64
 *
 * Implements mouse.h API as no-ops.
 * mouse_inject_absolute() works for future VirtIO-input (Phase 7).
 */

#include <kernel/mouse.h>

static int mouse_x = 0;
static int mouse_y = 0;
static uint8_t mouse_btns = 0;

void mouse_initialize(void) {
    mouse_x = 0;
    mouse_y = 0;
    mouse_btns = 0;
}

int mouse_get_x(void) { return mouse_x; }
int mouse_get_y(void) { return mouse_y; }
uint8_t mouse_get_buttons(void) { return mouse_btns; }

void mouse_get_delta(int *dx, int *dy) {
    if (dx) *dx = 0;
    if (dy) *dy = 0;
}

int mouse_poll(void) { return 0; }
int mouse_debug_irq_count(void) { return 0; }

void mouse_inject_absolute(int x, int y, uint8_t buttons) {
    mouse_x = x;
    mouse_y = y;
    mouse_btns = buttons;
}
