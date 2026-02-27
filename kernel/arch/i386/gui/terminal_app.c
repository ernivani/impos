/* terminal_app.c — GUI terminal emulator window.
 *
 * Creates a ui_window and points the TTY canvas at it.
 * Routes keyboard input through shell_handle_key().
 * Foreground apps (top, vi, etc.) get their on_key/on_tick callbacks.
 *
 * Pattern follows settings.c: singleton window, per-frame tick.
 */
#include <kernel/terminal_app.h>
#include <kernel/ui_window.h>
#include <kernel/gfx.h>
#include <kernel/tty.h>
#include <kernel/shell.h>
#include <kernel/idt.h>
#include <string.h>
#include <stdio.h>

/* ── Layout ─────────────────────────────────────────────────────── */
#define TERM_WIN_W  800
#define TERM_WIN_H  500
#define TERM_BG     0xFF1E1E2E   /* Catppuccin Mocha base */

/* ── State ──────────────────────────────────────────────────────── */
static int term_win_id = -1;
static int term_shell_inited = 0;

/* ── Repaint the terminal canvas ────────────────────────────────── */
static void term_damage(void) {
    if (term_win_id < 0) return;
    ui_window_damage_all(term_win_id);
}

/* ── Public API ─────────────────────────────────────────────────── */

void app_terminal_open(void) {
    if (term_win_id >= 0) {
        /* Already open: bring to front */
        ui_window_raise(term_win_id);
        ui_window_focus(term_win_id);
        return;
    }

    /* Create window centered */
    int sw = (int)gfx_width(), sh = (int)gfx_height();
    int wx = (sw - TERM_WIN_W) / 2;
    int wy = (sh - TERM_WIN_H) / 2;
    term_win_id = ui_window_create(wx, wy, TERM_WIN_W, TERM_WIN_H, "Terminal");

    /* Get canvas and bind TTY to it */
    int cw, ch;
    uint32_t *canvas = ui_window_canvas(term_win_id, &cw, &ch);
    if (canvas) {
        /* Clear canvas to background color */
        int total = cw * ch;
        for (int i = 0; i < total; i++)
            canvas[i] = TERM_BG;

        terminal_set_canvas(term_win_id, canvas, cw, ch);
        terminal_set_window_bg(TERM_BG);
        terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        terminal_set_cursor(0, 0);
    }

    /* Initialize shell on first open */
    if (!term_shell_inited) {
        shell_init_interactive();
        term_shell_inited = 1;
    }
    shell_draw_prompt();
    term_damage();
}

/* Handle a single keypress routed from the main event loop.
   Returns 1 if the key was consumed. */
int terminal_app_handle_key(char c) {
    if (term_win_id < 0) return 0;

    shell_fg_app_t *fg = shell_get_fg_app();
    if (fg) {
        /* Foreground app gets keys */
        if (fg->on_key) fg->on_key(c);
    } else {
        int r = shell_handle_key(c);
        if (r == 1) {
            /* Command ready — execute it */
            const char *cmd = shell_get_command();
            if (cmd[0]) {
                shell_history_add(cmd);
                shell_process_command((char *)cmd);
                if (shell_exit_requested) {
                    shell_exit_requested = 0;
                    /* Close terminal window on 'exit' */
                    ui_window_close_animated(term_win_id);
                    terminal_clear_canvas();
                    term_win_id = -1;
                    return 1;
                }
            }
            /* Only draw prompt if no fg app took over */
            if (!shell_get_fg_app())
                shell_draw_prompt();
        } else if (r == 2) {
            /* Ctrl+C or empty enter — redraw prompt */
            shell_draw_prompt();
        }
    }
    term_damage();
    return 1;
}

/* Per-frame tick: handles close request, canvas resize, fg app ticks.
   Returns 1 if a mouse click in the content area was consumed. */
int terminal_app_tick(int mx, int my, int btn_down, int btn_up) {
    if (term_win_id < 0) return 0;

    /* Handle close request */
    if (ui_window_close_requested(term_win_id)) {
        /* Clean up fg app if running */
        shell_fg_app_t *fg = shell_get_fg_app();
        if (fg && fg->on_close) fg->on_close();

        ui_window_close_clear(term_win_id);
        ui_window_close_animated(term_win_id);
        terminal_clear_canvas();
        term_win_id = -1;
        return 0;
    }

    /* Re-bind canvas pointer in case of resize */
    {
        int cw, ch;
        uint32_t *canvas = ui_window_canvas(term_win_id, &cw, &ch);
        if (canvas) {
            terminal_notify_canvas_resize(term_win_id, canvas, cw, ch);
        }
    }

    /* Tick foreground app (e.g. top refreshes periodically) */
    {
        shell_fg_app_t *fg = shell_get_fg_app();
        if (fg && fg->on_tick && fg->tick_interval > 0) {
            static uint32_t last_fg_tick = 0;
            uint32_t now = pit_get_ticks();
            if (now - last_fg_tick >= (uint32_t)fg->tick_interval) {
                last_fg_tick = now;
                fg->on_tick();
                term_damage();
            }
        }
    }

    /* Mouse: consume clicks on the content area to focus the window */
    ui_win_info_t info = ui_window_info(term_win_id);
    if (info.w <= 0) return 0;

    int lx = mx - info.cx;
    int ly = my - info.cy;
    if (lx >= 0 && ly >= 0 && lx < info.cw && ly < info.ch) {
        if (btn_down) {
            ui_window_focus(term_win_id);
            ui_window_raise(term_win_id);
            return 1;
        }
        if (btn_up) return 1;
    }

    return 0;
}

int terminal_app_win_open(void) {
    return term_win_id >= 0;
}

int terminal_app_win_id(void) {
    return term_win_id;
}
