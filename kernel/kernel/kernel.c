#include <stdio.h>
#include <kernel/tty.h>
#include <kernel/shell.h>

void kernel_main(void) {
    terminal_initialize();
    shell_initialize();

    char command_buffer[1024];
    size_t buffer_pos = 0;

    while (1) {
        printf("$ ");
        buffer_pos = 0;
        while (1) {
            char c = getchar();
            if (c == '\n') {
                command_buffer[buffer_pos] = '\0';
                printf("\n");
                break;
            } else if (c == '\b') {
                if (buffer_pos > 0) {
                    buffer_pos--;
                    putchar('\b');
                }
            } else if (c == '\t') {
                size_t old_pos = buffer_pos;
                size_t new_pos =
                    shell_autocomplete(command_buffer, buffer_pos, sizeof(command_buffer));
                for (size_t i = old_pos; i < new_pos; i++) {
                    putchar(command_buffer[i]);
                }
                buffer_pos = new_pos;
            } else if (buffer_pos < sizeof(command_buffer) - 1) {
                command_buffer[buffer_pos++] = c;
                putchar(c);
            }
        }

        if (buffer_pos > 0) {
            shell_process_command(command_buffer);
        }
    }
}
