/* input.c — Phase 5: Unified input event queue
 *
 * Replaces the current getchar() polling + IRQ flag approach.
 *
 * Event types:
 *   KEY_PRESS / KEY_RELEASE — scancode + Unicode codepoint + modifier mask
 *   MOUSE_MOVE              — absolute position (ax, ay) + delta (dx, dy)
 *   MOUSE_BTN               — button index + press/release + position
 *   SCROLL                  — wheel delta (dx, dy)
 *
 * Design:
 *   IRQ1  (keyboard) → input_push_key_event()   fills ring buffer
 *   IRQ12 (mouse)    → input_push_mouse_event()  fills ring buffer
 *   Frame loop       → input_drain()             WM reads and routes events
 *
 * Routing:
 *   Global shortcuts (Super, Alt+Tab, Ctrl+Alt+T) handled by WM first.
 *   Remaining events routed to focused window's on_event() callback.
 *   Mouse events routed to hovered window (not focused) for scroll.
 *
 * TODO (Phase 5 implementation):
 *   [ ] Define input_event_t with type + time_ms + union payload
 *   [ ] Ring buffer (256 events), lock-free single-producer (IRQ) / single-consumer (frame)
 *   [ ] input_init(): register IRQ1 + IRQ12 handlers
 *   [ ] input_push_key_event(scancode): translate → codepoint, push
 *   [ ] input_push_mouse_event(dx, dy, buttons): accumulate abs pos, push
 *   [ ] input_drain(handler_fn): dequeue all pending events, call handler
 *   [ ] Dead key / compose sequence support
 *   [ ] Scroll wheel routing to hovered window
 */

/* Phase 1 placeholder */
void input_init(void)  { }
void input_drain(void) { }
