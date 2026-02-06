#include <stdio.h>
#include <stdint.h>

#if defined(__is_libk)
#include <kernel/tty.h>

#define KEYBOARD_DATA_PORT    0x60
#define KEYBOARD_STATUS_PORT  0x64

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

#define CAPSLOCK_SCANCODE 0x3A
#define LEFT_SHIFT_SCANCODE 0x2A
#define RIGHT_SHIFT_SCANCODE 0x36

static int caps_lock_active = 0;
static int shift_pressed = 0;
static int extended_scancode = 0;

static const char azerty_normal[] = {
    0, 27,                                          // 0x00, 0x01 (null, escape)
    '&', 0, '"', '\'', '(', '-', 0, '_', 0, 0,     // 0x02-0x0B: 1-0 row
    ')', '=',                                       // 0x0C, 0x0D
    '\b', '\t',                                     // 0x0E, 0x0F
    'a', 'z', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', // 0x10-0x19
    '^', '$',                                       // 0x1A, 0x1B
    '\n', 0,                                        // 0x1C (enter), 0x1D (ctrl)
    'q', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l',  // 0x1E-0x26
    'm', 0, '*',                                    // 0x27, 0x28, 0x29
    0, '<',                                         // 0x2A (lshift), 0x2B
    'w', 'x', 'c', 'v', 'b', 'n',                  // 0x2C-0x31
    ',', ';', ':', '!',                             // 0x32-0x35
    0, 0, 0,                                        // 0x36 (rshift), 0x37, 0x38 (alt)
    ' ',                                            // 0x39
};

static const char azerty_shift[] = {
    0, 27,                                          // 0x00, 0x01
    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', // 0x02-0x0B
    0, '+',                                         // 0x0C, 0x0D
    '\b', '\t',                                     // 0x0E, 0x0F
    'A', 'Z', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', // 0x10-0x19
    0, 0,                                           // 0x1A, 0x1B
    '\n', 0,                                        // 0x1C, 0x1D
    'Q', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L',  // 0x1E-0x26
    'M', '%', 0,                                    // 0x27, 0x28, 0x29
    0, '>',                                         // 0x2A, 0x2B
    'W', 'X', 'C', 'V', 'B', 'N',                  // 0x2C-0x31
    '?', '.', '/', 0,                               // 0x32-0x35
    0, 0, 0,                                        // 0x36, 0x37, 0x38
    ' ',                                            // 0x39
};

#define KEYMAP_SIZE (sizeof(azerty_normal))

char getchar(void) {
    char c = 0;
    while (1) {
        if ((inb(KEYBOARD_STATUS_PORT) & 1) == 0) continue;

        uint8_t scancode = inb(KEYBOARD_DATA_PORT);

        if (scancode == 0xE0) {
            extended_scancode = 1;
            continue;
        }

        if (scancode & 0x80) {
            scancode &= 0x7F;
            if (extended_scancode) {
                extended_scancode = 0;
                continue;
            }
            if (scancode == LEFT_SHIFT_SCANCODE || scancode == RIGHT_SHIFT_SCANCODE) {
                shift_pressed = 0;
            }
            continue;
        }

        if (extended_scancode) {
            extended_scancode = 0;
            switch (scancode) {
                case 0x48: return KEY_UP;
                case 0x50: return KEY_DOWN;
                case 0x4B: return KEY_LEFT;
                case 0x4D: return KEY_RIGHT;
                default: continue;
            }
        }

        if (scancode == CAPSLOCK_SCANCODE) {
            caps_lock_active = !caps_lock_active;
            continue;
        }
        if (scancode == LEFT_SHIFT_SCANCODE || scancode == RIGHT_SHIFT_SCANCODE) {
            shift_pressed = 1;
            continue;
        }

        if (scancode >= KEYMAP_SIZE) continue;

        if (shift_pressed) {
            c = azerty_shift[scancode];
        } else {
            c = azerty_normal[scancode];
            if (caps_lock_active && c >= 'a' && c <= 'z') {
                c = c - 'a' + 'A';
            }
        }

        if (caps_lock_active && shift_pressed && c >= 'A' && c <= 'Z') {
            c = c - 'A' + 'a';
        }

        if (c == 0) continue;
        break;
    }
    return c;
}

#else
// TODO: Implement stdio and the read system call.
char getchar(void) {
    return EOF;
}
#endif
