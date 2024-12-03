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

char getchar(void) {
    char c;
    // Wait for keyboard input
    while (1) {
        // Check if there's input available
        if ((inb(KEYBOARD_STATUS_PORT) & 1) == 0) continue;
        
        // Read the scancode
        uint8_t scancode = inb(KEYBOARD_DATA_PORT);
        
        // Convert scancode to ASCII (this is a very basic mapping)
        // You might want to implement a more complete scancode-to-ASCII mapping
        if (scancode >= 2 && scancode <= 0x0B) {
            c = '0' + (scancode - 1);  // Numbers 1-9
            break;
        } else if (scancode >= 0x10 && scancode <= 0x19) {
            c = 'q' + (scancode - 0x10);  // First row of letters
            break;
        } else if (scancode >= 0x1E && scancode <= 0x26) {
            c = 'a' + (scancode - 0x1E);  // Second row of letters
            break;
        } else if (scancode >= 0x2C && scancode <= 0x32) {
            c = 'z' + (scancode - 0x2C);  // Third row of letters
            break;
        } else if (scancode == 0x1C) {
            c = '\n';  // Enter key
            break;
        } else if (scancode == 0x39) {
            c = ' ';  // Space key
            break;
        } else if (scancode == 0x0E) {
            c = '\b';  // Backspace key
            break;
        }
    }
    return c;
}

#else
// TODO: Implement stdio and the read system call.
char getchar(void) {
    return EOF;
}
#endif
