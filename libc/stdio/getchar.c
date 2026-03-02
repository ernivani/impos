#include <stdio.h>
#include <stdint.h>

#if defined(__is_libk)
#include <kernel/tty.h>
#include <kernel/io.h>
#include <kernel/idt.h>
#include <kernel/task.h>
#include <kernel/signal.h>

#define CAPSLOCK_SCANCODE    0x3A
#define NUMLOCK_SCANCODE     0x45
#define LEFT_SHIFT_SCANCODE  0x2A
#define RIGHT_SHIFT_SCANCODE 0x36
#define LEFT_CTRL_SCANCODE   0x1D
#define LEFT_ALT_SCANCODE    0x38

static int caps_lock_active = 0;
static int num_lock_active = 1;
static int shift_pressed = 0;
static int ctrl_pressed = 0;
static int alt_pressed = 0;
static int altgr_pressed = 0;
static int extended_scancode = 0;

/* ═══ Keyboard ring buffer (filled by IRQ1 handler) ═════════════ */

#define KBD_BUF_SIZE 128
static volatile uint8_t kbd_buf[KBD_BUF_SIZE];
static volatile int kbd_head = 0;
static volatile int kbd_tail = 0;

/* Secondary ring buffer for raw scancode consumers (DOOM).
 * Populated in parallel with kbd_buf so both ASCII translation
 * and raw scancode readers get every scancode independently. */
static volatile uint8_t raw_buf[KBD_BUF_SIZE];
static volatile int raw_head = 0;
static volatile int raw_tail = 0;

void keyboard_push_scancode(uint8_t scancode) {
    int next = (kbd_head + 1) % KBD_BUF_SIZE;
    if (next != kbd_tail) {
        kbd_buf[kbd_head] = scancode;
        kbd_head = next;
    }
    /* Mirror to raw buffer for keyboard_get_raw_scancode() */
    int rnext = (raw_head + 1) % KBD_BUF_SIZE;
    if (rnext != raw_tail) {
        raw_buf[raw_head] = scancode;
        raw_head = rnext;
    }
}

static int kbd_available(void) {
    return kbd_head != kbd_tail;
}

int keyboard_data_available(void) {
    return kbd_available();
}

static uint8_t kbd_pop(void) {
    uint8_t sc = kbd_buf[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
    return sc;
}

/* ═══ Idle callback (for mouse processing while waiting) ════════ */

static void (*idle_callback)(void) = 0;
static volatile int force_exit_flag = 0;

void keyboard_set_idle_callback(void (*cb)(void)) {
    idle_callback = cb;
}

int keyboard_force_exit(void) {
    if (force_exit_flag) {
        force_exit_flag = 0;
        return 1;
    }
    return 0;
}

void keyboard_request_force_exit(void) {
    force_exit_flag = 1;
}

/* Double-Ctrl detection for Finder */
static volatile uint32_t ctrl_release_tick = 0;
static volatile int ctrl_double_tap = 0;

int keyboard_check_double_ctrl(void) {
    if (ctrl_double_tap) {
        ctrl_double_tap = 0;
        return 1;
    }
    return 0;
}

void keyboard_run_idle(void) {
    if (idle_callback)
        idle_callback();
}

int keyboard_get_shift(void) { return shift_pressed; }
int keyboard_get_ctrl(void)  { return ctrl_pressed; }
int keyboard_get_alt(void)   { return alt_pressed; }

/* Raw scancode reader for Doom: returns raw PS/2 scancode (including
   release bit 7 and E0 prefix as 0xE0), or -1 if none available.
   Reads from a dedicated buffer so it doesn't steal from ASCII translation. */
int keyboard_get_raw_scancode(void) {
    if (raw_head == raw_tail) return -1;
    uint8_t sc = raw_buf[raw_tail];
    raw_tail = (raw_tail + 1) % KBD_BUF_SIZE;
    return (int)sc;
}

/* -------------------------------------------------------------------
 * Scancode-to-character tables  (PS/2 Scancode Set 1, indices 0x00-0x58)
 * 89 entries each.  Numpad keys (0x47-0x53) are handled separately.
 * CP437 encoding is used for characters > 127.
 * ------------------------------------------------------------------- */

/* CP437 codes for French accented characters */
#define C_EACUTE   ((char)0x82)  /* é */
#define C_EGRAVE   ((char)0x8A)  /* è */
#define C_CCEDIL   ((char)0x87)  /* ç */
#define C_AGRAVE   ((char)0x85)  /* à */
#define C_UGRAVE   ((char)0x97)  /* ù */
#define C_SQUARED  ((char)0xFD)  /* ² */
#define C_DEGREE   ((char)0xF8)  /* ° */
#define C_POUND    ((char)0x9C)  /* £ */
#define C_MICRO    ((char)0xE6)  /* µ */
#define C_SECTION  ((char)0x15)  /* § */

/* ========================  AZERTY (FR)  ======================== */

static const char fr_normal[89] = {
    0,    27,                                              /* 0x00 null, 0x01 ESC */
    '&',  C_EACUTE, '"', '\'', '(', '-',                  /* 0x02-0x07: & é " ' ( - */
    C_EGRAVE, '_', C_CCEDIL, C_AGRAVE,                    /* 0x08-0x0B: è _ ç à */
    ')',  '=',                                             /* 0x0C ), 0x0D = */
    '\b', '\t',                                            /* 0x0E backspace, 0x0F tab */
    'a',  'z',  'e',  'r',  't',                          /* 0x10-0x14 */
    'y',  'u',  'i',  'o',  'p',                          /* 0x15-0x19 */
    '^',  '$',                                             /* 0x1A, 0x1B */
    '\n', 0,                                               /* 0x1C enter, 0x1D ctrl */
    'q',  's',  'd',  'f',  'g',                          /* 0x1E-0x22 */
    'h',  'j',  'k',  'l',                                /* 0x23-0x26 */
    'm',  C_UGRAVE, C_SQUARED,                            /* 0x27 m, 0x28 ù, 0x29 ² */
    0,    '*',                                             /* 0x2A lshift, 0x2B * */
    'w',  'x',  'c',  'v',  'b',  'n',                   /* 0x2C-0x31 */
    ',',  ';',  ':',  '!',                                /* 0x32-0x35 */
    0,    '*',  0,    ' ',                                 /* 0x36 rshift, 0x37 kp*, 0x38 lalt, 0x39 space */
    0,                                                     /* 0x3A caps lock */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                         /* 0x3B-0x44 F1-F10 */
    0,    0,                                               /* 0x45 numlock, 0x46 scrolllock */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,               /* 0x47-0x53 numpad */
    0,    0,                                               /* 0x54-0x55 */
    '<',                                                   /* 0x56 ISO 102nd key */
    0,    0,                                               /* 0x57 F11, 0x58 F12 */
};

static const char fr_shift[89] = {
    0,    27,                                              /* 0x00, 0x01 */
    '1',  '2',  '3',  '4',  '5',  '6',                   /* 0x02-0x07 */
    '7',  '8',  '9',  '0',                                /* 0x08-0x0B */
    C_DEGREE, '+',                                         /* 0x0C °, 0x0D + */
    '\b', '\t',                                            /* 0x0E, 0x0F */
    'A',  'Z',  'E',  'R',  'T',                          /* 0x10-0x14 */
    'Y',  'U',  'I',  'O',  'P',                          /* 0x15-0x19 */
    0,    C_POUND,                                         /* 0x1A, 0x1B £ */
    '\n', 0,                                               /* 0x1C, 0x1D */
    'Q',  'S',  'D',  'F',  'G',                          /* 0x1E-0x22 */
    'H',  'J',  'K',  'L',                                /* 0x23-0x26 */
    'M',  '%',  0,                                         /* 0x27, 0x28, 0x29 */
    0,    C_MICRO,                                         /* 0x2A, 0x2B µ */
    'W',  'X',  'C',  'V',  'B',  'N',                   /* 0x2C-0x31 */
    '?',  '.',  '/',  C_SECTION,                           /* 0x32-0x35 */
    0,    '*',  0,    ' ',                                 /* 0x36-0x39 */
    0,                                                     /* 0x3A */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                         /* 0x3B-0x44 */
    0,    0,                                               /* 0x45, 0x46 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,               /* 0x47-0x53 */
    0,    0,                                               /* 0x54-0x55 */
    '>',                                                   /* 0x56 */
    0,    0,                                               /* 0x57, 0x58 */
};

static const char fr_altgr[89] = {
    0,    0,                                               /* 0x00, 0x01 */
    0,    '~',  '#',  '{',  '[',  '|',                    /* 0x02-0x07 */
    '`',  '\\', '^',  '@',                                /* 0x08-0x0B */
    ']',  '}',                                             /* 0x0C, 0x0D */
    0,    0,                                               /* 0x0E, 0x0F */
    0,    0,    0,    0,    0,                             /* 0x10-0x14 */
    0,    0,    0,    0,    0,                             /* 0x15-0x19 */
    0,    0,                                               /* 0x1A, 0x1B */
    0,    0,                                               /* 0x1C, 0x1D */
    0,    0,    0,    0,    0,                             /* 0x1E-0x22 */
    0,    0,    0,    0,                                   /* 0x23-0x26 */
    0,    0,    0,                                         /* 0x27-0x29 */
    0,    0,                                               /* 0x2A, 0x2B */
    0,    0,    0,    0,    0,    0,                       /* 0x2C-0x31 */
    0,    0,    0,    0,                                   /* 0x32-0x35 */
    0,    0,    0,    0,                                   /* 0x36-0x39 */
    0,                                                     /* 0x3A */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                         /* 0x3B-0x44 */
    0,    0,                                               /* 0x45, 0x46 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,               /* 0x47-0x53 */
    0,    0,                                               /* 0x54-0x55 */
    0,                                                     /* 0x56 */
    0,    0,                                               /* 0x57, 0x58 */
};

/* ========================  QWERTY (US)  ======================== */

static const char us_normal[89] = {
    0,    27,                                              /* 0x00, 0x01 */
    '1',  '2',  '3',  '4',  '5',  '6',                   /* 0x02-0x07 */
    '7',  '8',  '9',  '0',                                /* 0x08-0x0B */
    '-',  '=',                                             /* 0x0C, 0x0D */
    '\b', '\t',                                            /* 0x0E, 0x0F */
    'q',  'w',  'e',  'r',  't',                          /* 0x10-0x14 */
    'y',  'u',  'i',  'o',  'p',                          /* 0x15-0x19 */
    '[',  ']',                                             /* 0x1A, 0x1B */
    '\n', 0,                                               /* 0x1C, 0x1D */
    'a',  's',  'd',  'f',  'g',                          /* 0x1E-0x22 */
    'h',  'j',  'k',  'l',                                /* 0x23-0x26 */
    ';',  '\'', '`',                                       /* 0x27, 0x28, 0x29 */
    0,    '\\',                                            /* 0x2A lshift, 0x2B backslash */
    'z',  'x',  'c',  'v',  'b',  'n',                   /* 0x2C-0x31 */
    'm',  ',',  '.',  '/',                                 /* 0x32-0x35 */
    0,    '*',  0,    ' ',                                 /* 0x36 rshift, 0x37 kp*, 0x38 lalt, 0x39 space */
    0,                                                     /* 0x3A caps lock */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                         /* 0x3B-0x44 F1-F10 */
    0,    0,                                               /* 0x45, 0x46 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,               /* 0x47-0x53 */
    0,    0,                                               /* 0x54-0x55 */
    '\\',                                                  /* 0x56 ISO 102nd key */
    0,    0,                                               /* 0x57, 0x58 */
};

static const char us_shift[89] = {
    0,    27,                                              /* 0x00, 0x01 */
    '!',  '@',  '#',  '$',  '%',  '^',                    /* 0x02-0x07 */
    '&',  '*',  '(',  ')',                                 /* 0x08-0x0B */
    '_',  '+',                                             /* 0x0C, 0x0D */
    '\b', '\t',                                            /* 0x0E, 0x0F */
    'Q',  'W',  'E',  'R',  'T',                          /* 0x10-0x14 */
    'Y',  'U',  'I',  'O',  'P',                          /* 0x15-0x19 */
    '{',  '}',                                             /* 0x1A, 0x1B */
    '\n', 0,                                               /* 0x1C, 0x1D */
    'A',  'S',  'D',  'F',  'G',                          /* 0x1E-0x22 */
    'H',  'J',  'K',  'L',                                /* 0x23-0x26 */
    ':',  '"',  '~',                                       /* 0x27, 0x28, 0x29 */
    0,    '|',                                             /* 0x2A, 0x2B */
    'Z',  'X',  'C',  'V',  'B',  'N',                   /* 0x2C-0x31 */
    'M',  '<',  '>',  '?',                                /* 0x32-0x35 */
    0,    '*',  0,    ' ',                                 /* 0x36-0x39 */
    0,                                                     /* 0x3A */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                         /* 0x3B-0x44 */
    0,    0,                                               /* 0x45, 0x46 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,               /* 0x47-0x53 */
    0,    0,                                               /* 0x54-0x55 */
    '|',                                                   /* 0x56 */
    0,    0,                                               /* 0x57, 0x58 */
};

/* US layout has no AltGr - all zeros */
static const char us_altgr[89] = {0};

/* ========================  Layout selection  ======================== */

typedef struct {
    const char *normal;
    const char *shift;
    const char *altgr;
} kbd_layout_t;

static const kbd_layout_t layouts[] = {
    { fr_normal, fr_shift, fr_altgr },   /* KB_LAYOUT_FR = 0 */
    { us_normal, us_shift, us_altgr },   /* KB_LAYOUT_US = 1 */
};

#define NUM_LAYOUTS (sizeof(layouts) / sizeof(layouts[0]))

static int current_layout = KB_LAYOUT_FR;

void keyboard_set_layout(int layout) {
    if (layout >= 0 && (unsigned)layout < NUM_LAYOUTS)
        current_layout = layout;
}

int keyboard_get_layout(void) {
    return current_layout;
}

#define KEYMAP_SIZE 89

char getchar(void) {
    /* Serial console mode: read directly from COM1 */
    if (g_serial_console) {
        char c = serial_getc();
        if (c == '\r') c = '\n';  /* serial terminals send CR */
        return c;
    }

    /* Save caller's task so we can restore it when returning */
    int caller_task = task_get_current();

    while (1) {
        /* Check force-exit (WM close button clicked) */
        if (force_exit_flag) {
            force_exit_flag = 0;
            cpu_halting = 0;
            task_set_current(caller_task);
            return KEY_ESCAPE;
        }

        /* Read from interrupt-driven ring buffer */
        if (!kbd_available()) {
            /* HLT-based idle — works with preemptive scheduling because
               real PIT interrupts fire during HLT and run schedule() */
            task_set_current(TASK_IDLE);
            if (idle_callback) {
                cpu_halting = 0;  /* callback does real WM/mouse work */
                idle_callback();
            }
            task_set_current(TASK_IDLE); /* restore idle before HLT */
            cpu_halting = 1;      /* truly idle: about to HLT */
            __asm__ volatile ("hlt");
            continue;
        }

        cpu_halting = 0;
        task_set_current(caller_task);
        uint8_t scancode = kbd_pop();

        /* E0 prefix: next scancode is an extended key */
        if (scancode == 0xE0) {
            extended_scancode = 1;
            continue;
        }

        /* --- Key release (bit 7 set) --- */
        if (scancode & 0x80) {
            uint8_t released = scancode & 0x7F;
            if (extended_scancode) {
                extended_scancode = 0;
                if (released == 0x38)
                    altgr_pressed = 0;
                else if (released == LEFT_CTRL_SCANCODE) {
                    ctrl_pressed = 0;
                    uint32_t now = pit_get_ticks();
                    if (ctrl_release_tick > 0 && (now - ctrl_release_tick) < 30)
                        ctrl_double_tap = 1;
                    ctrl_release_tick = now;
                }
                continue;
            }
            if (released == LEFT_SHIFT_SCANCODE || released == RIGHT_SHIFT_SCANCODE)
                shift_pressed = 0;
            else if (released == LEFT_CTRL_SCANCODE) {
                ctrl_pressed = 0;
                /* Double-ctrl detection: check if within 30 ticks (300ms) */
                uint32_t now = pit_get_ticks();
                if (ctrl_release_tick > 0 && (now - ctrl_release_tick) < 30)
                    ctrl_double_tap = 1;
                ctrl_release_tick = now;
            }
            else if (released == LEFT_ALT_SCANCODE)
                alt_pressed = 0;
            continue;
        }

        /* --- Extended key press (after E0 prefix) --- */
        if (extended_scancode) {
            extended_scancode = 0;

            if (scancode == 0x38) { altgr_pressed = 1; continue; }  /* AltGr */
            if (scancode == LEFT_CTRL_SCANCODE) { ctrl_pressed = 1; continue; }
            if (scancode == 0x5B || scancode == 0x5C) return KEY_SUPER; /* Win keys */

            switch (scancode) {
                case 0x48: return KEY_UP;
                case 0x50: return KEY_DOWN;
                case 0x4B: return KEY_LEFT;
                case 0x4D: return KEY_RIGHT;
                case 0x47: return KEY_HOME;
                case 0x4F: return KEY_END;
                case 0x49: return KEY_PGUP;
                case 0x51: return KEY_PGDN;
                case 0x52: return KEY_INS;
                case 0x53: return KEY_DEL;
                case 0x1C: return '\n';   /* Keypad Enter */
                case 0x35: return '/';    /* Keypad / */
                default:   continue;
            }
        }

        /* --- Toggle keys --- */
        if (scancode == CAPSLOCK_SCANCODE) {
            caps_lock_active = !caps_lock_active;
            continue;
        }
        if (scancode == NUMLOCK_SCANCODE) {
            num_lock_active = !num_lock_active;
            continue;
        }

        /* --- Modifier keys --- */
        if (scancode == LEFT_SHIFT_SCANCODE || scancode == RIGHT_SHIFT_SCANCODE) {
            shift_pressed = 1;
            continue;
        }
        if (scancode == LEFT_CTRL_SCANCODE) {
            ctrl_pressed = 1;
            continue;
        }
        if (scancode == LEFT_ALT_SCANCODE) {
            alt_pressed = 1;
            continue;
        }

        /* Alt+Tab or Ctrl+Tab */
        if ((alt_pressed || ctrl_pressed) && scancode == 0x0F)
            return KEY_ALT_TAB;

        /* Ctrl+Space — open Finder */
        if (ctrl_pressed && scancode == 0x39)
            return KEY_FINDER;

        /* --- Numpad keys (0x47-0x53, no E0 = physical numpad) --- */
        if (scancode >= 0x47 && scancode <= 0x53) {
            if (num_lock_active) {
                switch (scancode) {
                    case 0x47: return '7';
                    case 0x48: return '8';
                    case 0x49: return '9';
                    case 0x4A: return '-';
                    case 0x4B: return '4';
                    case 0x4C: return '5';
                    case 0x4D: return '6';
                    case 0x4E: return '+';
                    case 0x4F: return '1';
                    case 0x50: return '2';
                    case 0x51: return '3';
                    case 0x52: return '0';
                    case 0x53: return '.';
                }
            } else {
                switch (scancode) {
                    case 0x4A: return '-';
                    case 0x4E: return '+';
                    case 0x47: return KEY_HOME;
                    case 0x48: return KEY_UP;
                    case 0x49: return KEY_PGUP;
                    case 0x4B: return KEY_LEFT;
                    case 0x4D: return KEY_RIGHT;
                    case 0x4F: return KEY_END;
                    case 0x50: return KEY_DOWN;
                    case 0x51: return KEY_PGDN;
                    case 0x52: return KEY_INS;
                    case 0x53: return KEY_DEL;
                    default:   continue;  /* 0x4C = numpad 5, no nav */
                }
            }
            continue;
        }

        /* F12 — toggle mobile view (mapped to Super) */
        if (scancode == 0x58)
            return KEY_SUPER;

        /* --- Regular character keys --- */
        if (scancode >= KEYMAP_SIZE)
            continue;

        const kbd_layout_t *lay = &layouts[current_layout];
        char c;

        if (altgr_pressed) {
            c = lay->altgr[scancode];
            if (c == 0) continue;
        } else if (ctrl_pressed) {
            c = lay->normal[scancode];
            if (c >= 'a' && c <= 'z') {
                if (c == 'c') {
                    /* Ctrl+C: send SIGINT to all killable user tasks */
                    for (int i = 4; i < TASK_MAX; i++) {
                        task_info_t *ti = task_get(i);
                        if (ti && ti->killable && ti->is_user)
                            sig_send(i, SIGINT);
                    }
                }
                return c - 'a' + 1;
            }
            /* Ctrl+non-letter (e.g. ctrl+backspace): return the base key;
               caller can check keyboard_get_ctrl() for modifier state */
            if (c != 0) return c;
        } else if (shift_pressed) {
            c = lay->shift[scancode];
        } else {
            c = lay->normal[scancode];
            if (caps_lock_active && c >= 'a' && c <= 'z')
                c = c - 'a' + 'A';
        }

        /* Caps Lock + Shift = lowercase */
        if (caps_lock_active && shift_pressed && c >= 'A' && c <= 'Z')
            c = c - 'A' + 'a';

        if (c == 0) continue;
        return c;
    }
}

/* Non-blocking getchar: processes available scancodes and returns
   the first printable character found, or 0 if none.  Never blocks. */
int keyboard_getchar_nb(void) {
    while (kbd_available()) {
        uint8_t scancode = kbd_pop();

        if (scancode == 0xE0) { extended_scancode = 1; continue; }

        /* Key release */
        if (scancode & 0x80) {
            uint8_t released = scancode & 0x7F;
            if (extended_scancode) {
                extended_scancode = 0;
                if (released == 0x38) altgr_pressed = 0;
                else if (released == LEFT_CTRL_SCANCODE) ctrl_pressed = 0;
                continue;
            }
            if (released == LEFT_SHIFT_SCANCODE || released == RIGHT_SHIFT_SCANCODE)
                shift_pressed = 0;
            else if (released == LEFT_CTRL_SCANCODE) ctrl_pressed = 0;
            else if (released == LEFT_ALT_SCANCODE) alt_pressed = 0;
            continue;
        }

        /* Extended key press */
        if (extended_scancode) {
            extended_scancode = 0;
            if (scancode == 0x38) { altgr_pressed = 1; continue; }
            if (scancode == LEFT_CTRL_SCANCODE) { ctrl_pressed = 1; continue; }
            switch (scancode) {
                case 0x48: return KEY_UP;
                case 0x50: return KEY_DOWN;
                case 0x4B: return KEY_LEFT;
                case 0x4D: return KEY_RIGHT;
                case 0x1C: return '\n';
                default:   continue;
            }
        }

        /* Modifiers / toggles */
        if (scancode == CAPSLOCK_SCANCODE) { caps_lock_active = !caps_lock_active; continue; }
        if (scancode == NUMLOCK_SCANCODE)  { num_lock_active = !num_lock_active; continue; }
        if (scancode == LEFT_SHIFT_SCANCODE || scancode == RIGHT_SHIFT_SCANCODE)
            { shift_pressed = 1; continue; }
        if (scancode == LEFT_CTRL_SCANCODE) { ctrl_pressed = 1; continue; }
        if (scancode == LEFT_ALT_SCANCODE)  { alt_pressed = 1; continue; }

        /* Regular character */
        if (scancode >= KEYMAP_SIZE) continue;
        const kbd_layout_t *lay = &layouts[current_layout];
        char c;
        if (altgr_pressed) {
            c = lay->altgr[scancode];
        } else if (shift_pressed) {
            c = lay->shift[scancode];
            if (caps_lock_active && c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        } else {
            c = lay->normal[scancode];
            if (caps_lock_active && c >= 'a' && c <= 'z') c = c - 'a' + 'A';
        }
        if (c == 0) continue;
        return (int)(unsigned char)c;
    }
    return 0;  /* Nothing available */
}

#else
/* Userspace stub */
char getchar(void) {
    return EOF;
}
void keyboard_set_layout(int layout) { (void)layout; }
int  keyboard_get_layout(void) { return 0; }
void keyboard_push_scancode(uint8_t sc) { (void)sc; }
void keyboard_set_idle_callback(void (*cb)(void)) { (void)cb; }
int  keyboard_force_exit(void) { return 0; }
void keyboard_request_force_exit(void) { }
int  keyboard_data_available(void) { return 0; }
int  keyboard_getchar_nb(void) { return 0; }
int  keyboard_check_double_ctrl(void) { return 0; }
void keyboard_run_idle(void) { }
int  keyboard_get_shift(void) { return 0; }
int  keyboard_get_ctrl(void)  { return 0; }
int  keyboard_get_alt(void)   { return 0; }
int  keyboard_get_raw_scancode(void) { return -1; }
#endif
