#include <stdio.h>
#include <stdint.h>

#if defined(__is_libk)
#include <kernel/tty.h>
#include <kernel/io.h>

#define KEYBOARD_DATA_PORT    0x60
#define KEYBOARD_STATUS_PORT  0x64

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
static int altgr_pressed = 0;
static int extended_scancode = 0;

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
    while (1) {
        if ((inb(KEYBOARD_STATUS_PORT) & 1) == 0)
            continue;

        uint8_t scancode = inb(KEYBOARD_DATA_PORT);

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
                else if (released == LEFT_CTRL_SCANCODE)
                    ctrl_pressed = 0;
                continue;
            }
            if (released == LEFT_SHIFT_SCANCODE || released == RIGHT_SHIFT_SCANCODE)
                shift_pressed = 0;
            else if (released == LEFT_CTRL_SCANCODE)
                ctrl_pressed = 0;
            continue;
        }

        /* --- Extended key press (after E0 prefix) --- */
        if (extended_scancode) {
            extended_scancode = 0;

            if (scancode == 0x38) { altgr_pressed = 1; continue; }  /* AltGr */
            if (scancode == LEFT_CTRL_SCANCODE) { ctrl_pressed = 1; continue; }

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
            continue;
        }

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
            if (c >= 'a' && c <= 'z')
                return c - 'a' + 1;  /* Ctrl+A=1 .. Ctrl+C=3 etc. */
            continue;
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

#else
/* Userspace stub */
char getchar(void) {
    return EOF;
}
void keyboard_set_layout(int layout) { (void)layout; }
int  keyboard_get_layout(void) { return 0; }
#endif
