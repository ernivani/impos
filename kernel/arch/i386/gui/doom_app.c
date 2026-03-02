/* doom_app.c — DOOM as a cooperative windowed app.
 *
 * Creates a 980×640 window (canvas ≈ 960×600 → 3× scale of 320×200).
 * Each ui_shell tick calls doom_app_tick() which runs one
 * doomgeneric_Tick() with setjmp protection against DOOM's exit().
 *
 * Shared globals (doom_windowed_mode, doom_canvas_buf, etc.) tell
 * doomgeneric_impos.c to render into the window canvas instead of
 * the raw framebuffer.
 */
#include <kernel/doom_app.h>
#include <kernel/ui_window.h>
#include <kernel/gfx.h>
#include <kernel/idt.h>
#include <kernel/menubar.h>
#include <setjmp.h>
#include <string.h>
#include <stdio.h>

/* ── Shared state with doomgeneric_impos.c ─────────────────────── */

int      doom_windowed_mode = 0;
uint32_t *doom_canvas_buf   = NULL;
int      doom_canvas_w      = 0;
int      doom_canvas_h      = 0;
int      doom_is_focused    = 0;

/* ── Window state ──────────────────────────────────────────────── */

static int doom_win_id      = -1;
static int doom_initialized = 0;
static jmp_buf doom_jmp;
static int doom_ticking     = 0;

/* ── External symbols ──────────────────────────────────────────── */

extern void     doomgeneric_Create(int argc, char **argv);
extern void     doomgeneric_Tick(void);
extern uint8_t *doom_wad_data;
extern uint32_t doom_wad_size;
extern void     exit_set_restart_point(void *env);
extern int      keyboard_get_raw_scancode(void);

/* ── Flush stale raw scancodes ─────────────────────────────────── */

static void flush_raw_scancodes(void)
{
    while (keyboard_get_raw_scancode() >= 0)
        ;
}

/* ── Open DOOM window ──────────────────────────────────────────── */

void doom_app_open(void)
{
    /* Already open — just raise */
    if (doom_win_id >= 0) {
        ui_window_raise(doom_win_id);
        return;
    }

    if (!doom_wad_data || doom_wad_size == 0) {
        printf("doom: no WAD file loaded (add doom1.wad as GRUB module)\n");
        return;
    }

    int sw = (int)gfx_width();
    int sh = (int)gfx_height();

    doom_win_id = ui_window_create(sw / 2 - 490, sh / 2 - 320,
                                    980, 640, "DOOM");
    if (doom_win_id < 0) return;

    doom_windowed_mode = 1;

    /* Set canvas buffer before init so DG_DrawFrame can render during
     * doomgeneric_Create()'s initial D_DoomLoop() → doomgeneric_Tick() */
    {
        int cw, ch;
        uint32_t *pix = ui_window_canvas(doom_win_id, &cw, &ch);
        if (pix) {
            doom_canvas_buf = pix;
            doom_canvas_w   = cw;
            doom_canvas_h   = ch;
        }
    }
    doom_is_focused = 1;

    if (!doom_initialized) {
        flush_raw_scancodes();

        exit_set_restart_point(&doom_jmp);
        doom_ticking = 1;

        if (setjmp(doom_jmp) == 0) {
            char *argv[] = { "doom", "-iwad", "doom1.wad", NULL };
            doomgeneric_Create(3, argv);
            doom_initialized = 1;
        }

        doom_ticking = 0;
        exit_set_restart_point(0);

        if (!doom_initialized) {
            /* DOOM called exit() during init — abort */
            ui_window_destroy(doom_win_id);
            doom_win_id = -1;
            doom_windowed_mode = 0;
            return;
        }
    }

    menubar_update_windows();
}

/* ── Per-frame tick ────────────────────────────────────────────── */

int doom_app_tick(int mx, int my, int btn_down, int btn_up)
{
    (void)mx; (void)my; (void)btn_down; (void)btn_up;

    if (doom_win_id < 0) return 0;

    /* Handle window close */
    if (ui_window_close_requested(doom_win_id)) {
        ui_window_close_clear(doom_win_id);
        ui_window_close_animated(doom_win_id);
        doom_win_id = -1;
        doom_canvas_buf = NULL;
        menubar_update_windows();
        return 0;
    }

    /* Update focus state for input routing.
     * Flush stale raw scancodes on focus gain so keypresses
     * from other apps don't bleed into DOOM. */
    {
        int was_focused = doom_is_focused;
        doom_is_focused = (ui_window_focused() == doom_win_id);
        if (doom_is_focused && !was_focused)
            flush_raw_scancodes();
    }

    /* Get current canvas buffer */
    int cw, ch;
    uint32_t *pix = ui_window_canvas(doom_win_id, &cw, &ch);
    if (!pix) return 0;

    doom_canvas_buf = pix;
    doom_canvas_w   = cw;
    doom_canvas_h   = ch;

    /* Run one DOOM tick with setjmp protection */
    exit_set_restart_point(&doom_jmp);
    doom_ticking = 1;

    if (setjmp(doom_jmp) == 0) {
        doomgeneric_Tick();
    } else {
        /* DOOM called exit() — close the window */
        doom_ticking = 0;
        exit_set_restart_point(0);
        ui_window_close_animated(doom_win_id);
        doom_win_id = -1;
        doom_canvas_buf = NULL;
        doom_initialized = 0;
        menubar_update_windows();
        return 0;
    }

    doom_ticking = 0;
    exit_set_restart_point(0);

    ui_window_damage_all(doom_win_id);
    return 0;
}

/* ── Exit trampoline (called by DOOM's I_Quit, I_Error, D_Endoom) ── */

void doom_exit_to_shell(void)
{
    if (doom_ticking)
        longjmp(doom_jmp, 1);
}

/* ── Queries ───────────────────────────────────────────────────── */

int doom_app_win_open(void) { return doom_win_id >= 0; }
int doom_app_win_id(void)   { return doom_win_id; }
