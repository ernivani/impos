/* calculator.c — Calculator app using widget toolkit (Phase 7.5)
 *
 * Classic calculator with 4x5 button grid + display label.
 * Uses integer arithmetic (no FPU) — numbers stored as long long
 * scaled by SCALE (10000) for 4 decimal places.
 */
#include <kernel/ui_widget.h>
#include <kernel/ui_window.h>
#include <kernel/ui_theme.h>
#include <kernel/gfx.h>
#include <string.h>
#include <stdio.h>

/* ── State ─────────────────────────────────────────────────────── */

static ui_window_t *calc_win = NULL;
static int display_idx = -1;

/* Integer-scaled fixed-point: value * SCALE */
#define SCALE 10000LL

static long long calc_accum   = 0;      /* accumulator */
static long long calc_current = 0;      /* current input */
static char      calc_op      = 0;      /* pending operator: +, -, *, / */
static int       calc_has_dot = 0;      /* decimal point entered */
static int       calc_dot_div = 1;      /* divisor for fractional digits */
static int       calc_fresh   = 1;      /* display shows result, next digit replaces */

static char calc_display[32] = "0";

static void calc_update_display(long long val)
{
    int neg = 0;
    if (val < 0) { neg = 1; val = -val; }

    long long whole = val / SCALE;
    long long frac  = val % SCALE;

    char buf[32];
    int pos = 0;

    if (neg) buf[pos++] = '-';

    /* Write whole part */
    char tmp[16]; int ti = 0;
    if (whole == 0) { tmp[ti++] = '0'; }
    else { while (whole > 0) { tmp[ti++] = '0' + (int)(whole % 10); whole /= 10; } }
    while (ti > 0) buf[pos++] = tmp[--ti];

    /* Write fractional part if non-zero */
    if (frac > 0) {
        buf[pos++] = '.';
        /* 4 digits, strip trailing zeros */
        char fb[5];
        fb[0] = '0' + (int)((frac / 1000) % 10);
        fb[1] = '0' + (int)((frac / 100) % 10);
        fb[2] = '0' + (int)((frac / 10) % 10);
        fb[3] = '0' + (int)(frac % 10);
        fb[4] = '\0';
        int last = 3;
        while (last > 0 && fb[last] == '0') last--;
        int j;
        for (j = 0; j <= last; j++) buf[pos++] = fb[j];
    }

    buf[pos] = '\0';
    strncpy(calc_display, buf, sizeof(calc_display) - 1);
    calc_display[sizeof(calc_display) - 1] = '\0';

    if (calc_win && display_idx >= 0) {
        ui_widget_t *w = ui_get_widget(calc_win, display_idx);
        if (w) strncpy(w->label.text, calc_display, UI_TEXT_MAX - 1);
        calc_win->dirty = 1;
    }
}

static long long calc_do_op(long long a, long long b, char op)
{
    switch (op) {
    case '+': return a + b;
    case '-': return a - b;
    case '*': return (a / 100) * (b / 100);  /* avoid overflow */
    case '/': return (b != 0) ? (a * SCALE) / b : 0;
    default:  return b;
    }
}

static void calc_press_digit(int d)
{
    if (calc_fresh) {
        calc_current = 0;
        calc_has_dot = 0;
        calc_dot_div = 1;
        calc_fresh = 0;
    }
    if (calc_has_dot) {
        if (calc_dot_div < (int)SCALE) {
            calc_dot_div *= 10;
            long long frac_add = (long long)d * (SCALE / calc_dot_div);
            if (calc_current >= 0)
                calc_current += frac_add;
            else
                calc_current -= frac_add;
        }
    } else {
        int neg = (calc_current < 0);
        long long abs_val = neg ? -calc_current : calc_current;
        abs_val = (abs_val / SCALE) * 10 + d;
        calc_current = (neg ? -1 : 1) * abs_val * SCALE;
    }
    calc_update_display(calc_current);
}

static void calc_press_op(char op)
{
    if (calc_op && !calc_fresh) {
        calc_accum = calc_do_op(calc_accum, calc_current, calc_op);
    } else {
        calc_accum = calc_current;
    }
    calc_op = op;
    calc_fresh = 1;
    calc_update_display(calc_accum);
}

static void calc_press_equals(void)
{
    if (calc_op) {
        calc_accum = calc_do_op(calc_accum, calc_current, calc_op);
        calc_op = 0;
    } else {
        calc_accum = calc_current;
    }
    calc_current = calc_accum;
    calc_fresh = 1;
    calc_update_display(calc_accum);
}

static void calc_press_clear(void)
{
    calc_accum = 0;
    calc_current = 0;
    calc_op = 0;
    calc_has_dot = 0;
    calc_dot_div = 1;
    calc_fresh = 1;
    calc_update_display(0);
}

static void calc_press_dot(void)
{
    if (calc_fresh) {
        calc_current = 0;
        calc_fresh = 0;
    }
    calc_has_dot = 1;
    /* Don't update display yet — wait for next digit */
}

static void calc_press_negate(void)
{
    calc_current = -calc_current;
    calc_update_display(calc_current);
}

/* ── Button callbacks ──────────────────────────────────────────── */

static void cb_digit(ui_window_t *win, int idx)
{
    (void)win;
    ui_widget_t *w = ui_get_widget(win, idx);
    if (w) calc_press_digit(w->button.text[0] - '0');
}

static void cb_op(ui_window_t *win, int idx)
{
    (void)win;
    ui_widget_t *w = ui_get_widget(win, idx);
    if (!w) return;
    char c = w->button.text[0];
    /* Map display chars to internal ops */
    if (c == 'x') c = '*';
    calc_press_op(c);
}

static void cb_eq(ui_window_t *win, int idx)
{
    (void)win; (void)idx;
    calc_press_equals();
}

static void cb_clear(ui_window_t *win, int idx)
{
    (void)win; (void)idx;
    calc_press_clear();
}

static void cb_dot(ui_window_t *win, int idx)
{
    (void)win; (void)idx;
    calc_press_dot();
}

static void cb_neg(ui_window_t *win, int idx)
{
    (void)win; (void)idx;
    calc_press_negate();
}

/* ── Public API ────────────────────────────────────────────────── */

void app_calculator_open(void)
{
    if (calc_win) {
        ui_window_focus(calc_win->wm_id);
        ui_window_raise(calc_win->wm_id);
        return;
    }

    int w = 260, h = 340;
    int sw = (int)gfx_width(), sh = (int)gfx_height();
    calc_win = uw_create(sw / 2 - w / 2, sh / 2 - h / 2, w, h, "Calculator");
    if (!calc_win) return;

    calc_press_clear();

    /* Display label — right-aligned (we draw left, but text is short enough) */
    display_idx = ui_add_label(calc_win, 12, 8, 224, 32, "0",
                               ui_theme.text_primary);

    /* Button grid: 4 columns x 5 rows */
    int bw = 52, bh = 38, gap = 6;
    int ox = 12, oy = 52;

    /* Row 0: C, +/-, %, / */
    ui_add_button(calc_win, ox + 0*(bw+gap), oy, bw, bh, "C", cb_clear);
    ui_add_button(calc_win, ox + 1*(bw+gap), oy, bw, bh, "+/-", cb_neg);
    ui_add_button(calc_win, ox + 2*(bw+gap), oy, bw, bh, "%", cb_op);
    ui_add_button(calc_win, ox + 3*(bw+gap), oy, bw, bh, "/", cb_op);

    oy += bh + gap;
    /* Row 1: 7 8 9 x */
    ui_add_button(calc_win, ox + 0*(bw+gap), oy, bw, bh, "7", cb_digit);
    ui_add_button(calc_win, ox + 1*(bw+gap), oy, bw, bh, "8", cb_digit);
    ui_add_button(calc_win, ox + 2*(bw+gap), oy, bw, bh, "9", cb_digit);
    ui_add_button(calc_win, ox + 3*(bw+gap), oy, bw, bh, "x", cb_op);

    oy += bh + gap;
    /* Row 2: 4 5 6 - */
    ui_add_button(calc_win, ox + 0*(bw+gap), oy, bw, bh, "4", cb_digit);
    ui_add_button(calc_win, ox + 1*(bw+gap), oy, bw, bh, "5", cb_digit);
    ui_add_button(calc_win, ox + 2*(bw+gap), oy, bw, bh, "6", cb_digit);
    ui_add_button(calc_win, ox + 3*(bw+gap), oy, bw, bh, "-", cb_op);

    oy += bh + gap;
    /* Row 3: 1 2 3 + */
    ui_add_button(calc_win, ox + 0*(bw+gap), oy, bw, bh, "1", cb_digit);
    ui_add_button(calc_win, ox + 1*(bw+gap), oy, bw, bh, "2", cb_digit);
    ui_add_button(calc_win, ox + 2*(bw+gap), oy, bw, bh, "3", cb_digit);
    ui_add_button(calc_win, ox + 3*(bw+gap), oy, bw, bh, "+", cb_op);

    oy += bh + gap;
    /* Row 4: 0 (wide), ., = */
    ui_add_button(calc_win, ox, oy, bw*2 + gap, bh, "0", cb_digit);
    ui_add_button(calc_win, ox + 2*(bw+gap), oy, bw, bh, ".", cb_dot);
    ui_add_button(calc_win, ox + 3*(bw+gap), oy, bw, bh, "=", cb_eq);

    uw_redraw(calc_win);
}

int calculator_tick(int mx, int my, int btn_down, int btn_up)
{
    if (!calc_win) return 0;
    int r = uw_tick(calc_win, mx, my, btn_down, btn_up, 0);
    /* Check if window was closed by uw_tick */
    if (calc_win && calc_win->wm_id < 0) {
        calc_win = NULL;
        display_idx = -1;
    }
    return r;
}

int calculator_win_open(void) { return calc_win != NULL; }
