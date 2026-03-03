#ifndef _KERNEL_SHELL_CMD_H
#define _KERNEL_SHELL_CMD_H

#include <stdint.h>

typedef void (*cmd_func_t)(int argc, char* argv[]);

#define CMD_FLAG_ROOT  (1 << 0)

typedef struct {
    const char* name;
    cmd_func_t func;
    const char* short_desc;
    const char* help_text;
    const char* man_page;
    uint8_t flags;
} command_t;

/* Each cmd_*.c exports its command group */
const command_t *cmd_fs_commands(int *count);
const command_t *cmd_text_commands(int *count);
const command_t *cmd_net_commands(int *count);
const command_t *cmd_user_commands(int *count);
const command_t *cmd_system_commands(int *count);
const command_t *cmd_gfx_commands(int *count);
const command_t *cmd_process_commands(int *count);
const command_t *cmd_exec_commands(int *count);

/* Shared helper: read file or pipe input, returns malloc'd buffer */
char *shell_read_input(const char *filename, int *out_len);

/* Pipe input remaining bytes (for shell_read_input) */
int shell_pipe_input_remaining(void);

#endif
