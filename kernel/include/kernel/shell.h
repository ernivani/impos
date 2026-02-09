#ifndef _KERNEL_SHELL_H
#define _KERNEL_SHELL_H

#include <stddef.h>

#define SHELL_HIST_SIZE   16
#define SHELL_CMD_SIZE    256

void shell_initialize(void);
void shell_initialize_subsystems(void);
int shell_needs_setup(void);
void shell_process_command(char* command);
size_t shell_autocomplete(char* buffer, size_t buffer_pos, size_t buffer_size);
int shell_login(void);

/* History management (used by kernel line editor + history command) */
void        shell_history_add(const char *cmd);
int         shell_history_count(void);
const char *shell_history_entry(int index);

/* Desktop terminal integration */
extern int shell_exit_requested;

#endif
