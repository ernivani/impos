/* cmd_gfx.c — aarch64 stub (no GUI on serial console) */
#include <kernel/shell_cmd.h>

static const command_t gfx_commands[] = {};

const command_t *cmd_gfx_commands(int *count) {
    *count = 0;
    return gfx_commands;
}
