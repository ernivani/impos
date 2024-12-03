#include <stdio.h>
#include <kernel/tty.h>
#include <kernel/shell.h>

void kernel_main(void) {
    terminal_initialize();

    char command_buffer[1024];
    size_t buffer_pos = 0;

    while (1) {
        // Simple command input
        buffer_pos = 0;
        while (1) {
            char c = getchar();
            if (c == '\n') {
                command_buffer[buffer_pos] = '\0';
                printf("\n");
                break;
            } else if (c == '\b' && buffer_pos > 0) {
                buffer_pos--;
                printf("\b \b");
            } else if (buffer_pos < sizeof(command_buffer) - 1) {
                command_buffer[buffer_pos++] = c;
                putchar(c);
            }
        }

    }
}