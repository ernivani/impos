#include <stdio.h>
#include <stdint.h>

#if defined(__is_libk)
#include <kernel/tty.h>

// Basic keyboard controller ports
#define KEYBOARD_DATA_PORT    0x60
#define KEYBOARD_STATUS_PORT  0x64

// Function to read from a port
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// Add these at the top with other definitions
#define CAPSLOCK_SCANCODE 0x3A
#define LEFT_SHIFT_SCANCODE 0x2A
#define RIGHT_SHIFT_SCANCODE 0x36

// Add these global variables
static int caps_lock_active = 0;
static int shift_pressed = 0;

char getchar(void) {
    char c = 0;
    while (1) {
        if ((inb(KEYBOARD_STATUS_PORT) & 1) == 0) continue;
        
        uint8_t scancode = inb(KEYBOARD_DATA_PORT);
        
        // Handle key releases
        if (scancode & 0x80) {
            scancode &= 0x7F;  // Clear the released bit
            if (scancode == LEFT_SHIFT_SCANCODE || scancode == RIGHT_SHIFT_SCANCODE) {
                shift_pressed = 0;
            }
            continue;
        }

        // Handle modifier keys
        if (scancode == CAPSLOCK_SCANCODE) {
            caps_lock_active = !caps_lock_active;
            continue;
        }
        if (scancode == LEFT_SHIFT_SCANCODE || scancode == RIGHT_SHIFT_SCANCODE) {
            shift_pressed = 1;
            continue;
        }

        // Convert scancode to ASCII
        switch (scancode) {
            // Numbers row (1234567890) with shift
            case 0x02 ... 0x0A:
                if (shift_pressed) {
                    static const char shift_nums[] = "!@#$%^&*()";
                    c = shift_nums[scancode - 0x02];
                } else {
                    c = '1' + (scancode - 0x02);
                }
                break;
            case 0x0B:
                c = shift_pressed ? ')' : '0';
                break;
            
            // Letters
            case 0x10 ... 0x19:  // Q to P
            case 0x1E ... 0x26:  // A to L
            case 0x2C ... 0x32:  // Z to M
                {
                    static const char letters[] = "qwertyuiopasdfghjklzxcvbnm";
                    int idx;
                    if (scancode <= 0x19) idx = scancode - 0x10;
                    else if (scancode <= 0x26) idx = scancode - 0x1E + 10;
                    else idx = scancode - 0x2C + 20;
                    
                    c = letters[idx];
                    // Apply caps if either caps lock is on (and shift isn't) or shift is pressed (and caps lock isn't)
                    if (caps_lock_active ^ shift_pressed) {
                        c = c - 'a' + 'A';
                    }
                }
                break;
            
            // Special keys (unchanged)
            case 0x1C: c = '\n'; break;    // Enter
            case 0x39: c = ' '; break;     // Space
            case 0x0E: c = '\b'; break;    // Backspace
            case 0x0F: c = '\t'; break;    // Tab
            
            // Punctuation with shift
            case 0x27: c = shift_pressed ? ':' : ';'; break;
            case 0x28: c = shift_pressed ? '"' : '\''; break;
            case 0x29: c = shift_pressed ? '~' : '`'; break;
            case 0x2B: c = shift_pressed ? '|' : '\\'; break;
            case 0x33: c = shift_pressed ? '<' : ','; break;
            case 0x34: c = shift_pressed ? '>' : '.'; break;
            case 0x35: c = shift_pressed ? '?' : '/'; break;
            case 0x0C: c = shift_pressed ? '_' : '-'; break;
            case 0x0D: c = shift_pressed ? '+' : '='; break;
            case 0x1A: c = shift_pressed ? '{' : '['; break;
            case 0x1B: c = shift_pressed ? '}' : ']'; break;
            
            default: continue;  // Ignore unhandled keys
        }
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
