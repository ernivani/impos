// doomgeneric_impos.c — ImposOS platform implementation for doomgeneric
//
// Implements the 5 required callbacks:
//   DG_Init(), DG_DrawFrame(), DG_SleepMs(), DG_GetTicksMs(), DG_GetKey()
// Plus DG_SetWindowTitle() (no-op).
//
// Two rendering modes:
//   Fullscreen: nearest-neighbor scales 320x200 to the framebuffer (legacy)
//   Windowed:   scales 320x200 to a ui_window canvas buffer
//
// Input: reads raw PS/2 scancodes from the keyboard ring buffer.
// Timer: uses PIT at 120 Hz.

#include "doomgeneric.h"
#include "doomkeys.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <kernel/gfx.h>
#include <kernel/idt.h>    /* pit_get_ticks(), pit_sleep_ms() */
#include <kernel/io.h>     /* DBG() */

/* ═══ Windowed mode state (set by doom_app.c) ═════════════════════ */

extern int      doom_windowed_mode;
extern uint32_t *doom_canvas_buf;
extern int      doom_canvas_w, doom_canvas_h;
extern int      doom_is_focused;

/* ═══ Scaling state ════════════════════════════════════════════════ */

static int doom_scale;
static int doom_offset_x;
static int doom_offset_y;

/* ═══ Key queue ════════════════════════════════════════════════════ */

#define KEYQUEUE_SIZE 32

static unsigned short s_KeyQueue[KEYQUEUE_SIZE];
static unsigned int s_KeyQueueWriteIndex = 0;
static unsigned int s_KeyQueueReadIndex = 0;

static void addKeyToQueue(int pressed, unsigned char doomKey)
{
    if (doomKey == 0) return;
    unsigned short keyData = (unsigned short)((pressed << 8) | doomKey);
    s_KeyQueue[s_KeyQueueWriteIndex] = keyData;
    s_KeyQueueWriteIndex = (s_KeyQueueWriteIndex + 1) % KEYQUEUE_SIZE;
}

/* ═══ PS/2 scancode → Doom key mapping ════════════════════════════ */

/* keyboard_get_raw_scancode() declared in <stdio.h> */

static int e0_prefix = 0;

/* Map a PS/2 make scancode (with E0 context) to a doom key constant.
   Returns 0 if the scancode doesn't map to a doom key. */
static unsigned char scancode_to_doom(uint8_t sc, int is_e0)
{
    if (is_e0) {
        switch (sc) {
            case 0x48: return KEY_UPARROW;
            case 0x50: return KEY_DOWNARROW;
            case 0x4B: return KEY_LEFTARROW;
            case 0x4D: return KEY_RIGHTARROW;
            case 0x1C: return KEY_ENTER;    /* keypad Enter */
            case 0x1D: return KEY_RCTRL;    /* right Ctrl */
            case 0x38: return KEY_RALT;     /* right Alt / AltGr */
            default:   return 0;
        }
    }

    switch (sc) {
        case 0x01: return KEY_ESCAPE;
        case 0x1C: return KEY_ENTER;
        case 0x39: return ' ';              /* space = use */
        case 0x1D: return KEY_RCTRL;        /* left Ctrl = fire */
        case 0x2A: return KEY_RSHIFT;       /* left Shift = run */
        case 0x36: return KEY_RSHIFT;       /* right Shift = run */
        case 0x38: return KEY_RALT;         /* left Alt = strafe */
        case 0x0F: return KEY_TAB;          /* Tab = automap */
        case 0x0E: return KEY_BACKSPACE;

        /* Number row: 1-9,0 → weapon select */
        case 0x02: return '1';
        case 0x03: return '2';
        case 0x04: return '3';
        case 0x05: return '4';
        case 0x06: return '5';
        case 0x07: return '6';
        case 0x08: return '7';
        case 0x09: return '8';
        case 0x0A: return '9';
        case 0x0B: return '0';

        case 0x0C: return KEY_MINUS;
        case 0x0D: return KEY_EQUALS;

        /* Letter keys — doom uses lowercase ASCII */
        case 0x10: return 'q';  /* AZERTY: a */
        case 0x11: return 'w';  /* AZERTY: z */
        case 0x12: return 'e';
        case 0x13: return 'r';
        case 0x14: return 't';
        case 0x15: return 'y';
        case 0x16: return 'u';
        case 0x17: return 'i';
        case 0x18: return 'o';
        case 0x19: return 'p';
        case 0x1E: return 'a';  /* AZERTY: q */
        case 0x1F: return 's';
        case 0x20: return 'd';
        case 0x21: return 'f';
        case 0x22: return 'g';
        case 0x23: return 'h';
        case 0x24: return 'j';
        case 0x25: return 'k';
        case 0x26: return 'l';
        case 0x27: return ';';
        case 0x2C: return 'z';  /* AZERTY: w */
        case 0x2D: return 'x';
        case 0x2E: return 'c';
        case 0x2F: return 'v';
        case 0x30: return 'b';
        case 0x31: return 'n';
        case 0x32: return 'm';
        case 0x33: return ',';
        case 0x34: return '.';

        /* F1-F12 */
        case 0x3B: return KEY_F1;
        case 0x3C: return KEY_F2;
        case 0x3D: return KEY_F3;
        case 0x3E: return KEY_F4;
        case 0x3F: return KEY_F5;
        case 0x40: return KEY_F6;
        case 0x41: return KEY_F7;
        case 0x42: return KEY_F8;
        case 0x43: return KEY_F9;
        case 0x44: return KEY_F10;
        case 0x57: return KEY_F11;
        case 0x58: return KEY_F12;

        /* Pause */
        case 0x46: return KEY_PAUSE;

        default:   return 0;
    }
}

/* ═══ Input polling ═══════════════════════════════════════════════ */

static void doom_poll_input(void)
{
    int sc;
    while ((sc = keyboard_get_raw_scancode()) >= 0) {
        uint8_t raw = (uint8_t)sc;

        DBG("[doom-input] raw=0x%02x", raw);

        /* E0 prefix byte */
        if (raw == 0xE0) {
            e0_prefix = 1;
            continue;
        }
        /* E1 prefix (Pause key) — skip next 2 bytes */
        if (raw == 0xE1) {
            keyboard_get_raw_scancode();
            keyboard_get_raw_scancode();
            addKeyToQueue(1, KEY_PAUSE);
            addKeyToQueue(0, KEY_PAUSE);
            continue;
        }

        int released = (raw & 0x80) != 0;
        uint8_t make = raw & 0x7F;

        unsigned char dk = scancode_to_doom(make, e0_prefix);
        e0_prefix = 0;

        if (dk != 0) {
            DBG("[doom-input] %s sc=0x%02x → dk=%d('%c')",
                released ? "UP" : "DN", make, dk,
                (dk >= 32 && dk < 127) ? dk : '?');
            addKeyToQueue(released ? 0 : 1, dk);
        }
    }
}

/* ═══ DG Callbacks ═════════════════════════════════════════════════ */

void DG_Init(void)
{
    e0_prefix = 0;
    s_KeyQueueWriteIndex = 0;
    s_KeyQueueReadIndex = 0;

    if (doom_windowed_mode) {
        /* Scale computed per-frame in DG_DrawFrame based on canvas size */
        doom_scale = 3;
        doom_offset_x = 0;
        doom_offset_y = 0;
        printf("DOOM: windowed mode (3x scale)\n");
        return;
    }

    /* Fullscreen: scale to fill the framebuffer */
    int scr_w = (int)gfx_width();
    int scr_h = (int)gfx_height();

    int scale_x = scr_w / DOOMGENERIC_RESX;
    int scale_y = scr_h / DOOMGENERIC_RESY;
    doom_scale = (scale_x < scale_y) ? scale_x : scale_y;
    if (doom_scale < 1) doom_scale = 1;

    doom_offset_x = (scr_w - DOOMGENERIC_RESX * doom_scale) / 2;
    doom_offset_y = (scr_h - DOOMGENERIC_RESY * doom_scale) / 2;

    gfx_clear(0x000000);
    gfx_flip();

    printf("DOOM: scale=%dx, offset=(%d,%d), screen=%dx%d\n",
           doom_scale, doom_offset_x, doom_offset_y, scr_w, scr_h);
}

void DG_DrawFrame(void)
{
    if (doom_windowed_mode) {
        /* ── Windowed: render to canvas buffer ──────────────────── */
        if (doom_is_focused)
            doom_poll_input();

        if (!doom_canvas_buf) return;

        int cw = doom_canvas_w;
        int ch = doom_canvas_h;

        /* Compute best integer scale for current canvas */
        int scale_x = cw / DOOMGENERIC_RESX;
        int scale_y = ch / DOOMGENERIC_RESY;
        int scale = (scale_x < scale_y) ? scale_x : scale_y;
        if (scale < 1) scale = 1;

        int ox = (cw - DOOMGENERIC_RESX * scale) / 2;
        int oy = (ch - DOOMGENERIC_RESY * scale) / 2;

        /* Clear canvas to black */
        for (int i = 0; i < cw * ch; i++)
            doom_canvas_buf[i] = 0xFF000000;

        /* Nearest-neighbor scale 320×200 → canvas.
         * DOOM's cmap_to_fb() writes 0x00RRGGBB (alpha=0), so we
         * force alpha=0xFF to make pixels opaque for the compositor. */
        pixel_t *src = DG_ScreenBuffer;
        for (int sy = 0; sy < DOOMGENERIC_RESY; sy++) {
            uint32_t *src_row = (uint32_t *)&src[sy * DOOMGENERIC_RESX];
            int dst_y_base = oy + sy * scale;

            for (int sx = 0; sx < DOOMGENERIC_RESX; sx++) {
                uint32_t pixel = src_row[sx] | 0xFF000000;
                int dst_x_base = ox + sx * scale;

                for (int dy = 0; dy < scale; dy++) {
                    int row = dst_y_base + dy;
                    if (row >= ch) break;
                    uint32_t *dst = &doom_canvas_buf[row * cw + dst_x_base];
                    for (int dx = 0; dx < scale && dst_x_base + dx < cw; dx++)
                        dst[dx] = pixel;
                }
            }
        }
        return;
    }

    /* ── Fullscreen: render to framebuffer ──────────────────────── */
    doom_poll_input();

    uint32_t *backbuf = gfx_backbuffer();
    uint32_t pitch = gfx_pitch() / 4;
    pixel_t *src = DG_ScreenBuffer;

    for (int sy = 0; sy < DOOMGENERIC_RESY; sy++) {
        uint32_t *src_row = (uint32_t *)&src[sy * DOOMGENERIC_RESX];
        int dst_y_base = doom_offset_y + sy * doom_scale;

        for (int sx = 0; sx < DOOMGENERIC_RESX; sx++) {
            uint32_t pixel = src_row[sx];
            int dst_x_base = doom_offset_x + sx * doom_scale;

            for (int dy = 0; dy < doom_scale; dy++) {
                uint32_t *dst = &backbuf[(dst_y_base + dy) * pitch + dst_x_base];
                for (int dx = 0; dx < doom_scale; dx++) {
                    dst[dx] = pixel;
                }
            }
        }
    }

    gfx_flip_rect(doom_offset_x, doom_offset_y,
                   DOOMGENERIC_RESX * doom_scale,
                   DOOMGENERIC_RESY * doom_scale);
}

void DG_SleepMs(uint32_t ms)
{
    if (doom_windowed_mode) return;   /* no-op: compositor hlt provides pacing */
    pit_sleep_ms(ms);
}

uint32_t DG_GetTicksMs(void)
{
    /* PIT runs at 120 Hz, so ticks × (1000/120) = milliseconds */
    return (uint32_t)(pit_get_ticks() * 1000 / 120);
}

int DG_GetKey(int *pressed, unsigned char *doomKey)
{
    if (s_KeyQueueReadIndex == s_KeyQueueWriteIndex)
        return 0;

    unsigned short keyData = s_KeyQueue[s_KeyQueueReadIndex];
    s_KeyQueueReadIndex = (s_KeyQueueReadIndex + 1) % KEYQUEUE_SIZE;

    *pressed = keyData >> 8;
    *doomKey = keyData & 0xFF;

    return 1;
}

void DG_SetWindowTitle(const char *title)
{
    (void)title;
}
