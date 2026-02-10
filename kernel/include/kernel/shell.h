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

/* Per-key shell API (non-blocking terminal integration) */
void        shell_init_interactive(void);
void        shell_draw_prompt(void);
int         shell_handle_key(char c);   /* 0=continue, 1=execute, 2=reprompt */
const char *shell_get_command(void);

/* Foreground app: non-blocking command running in the terminal */
typedef struct {
    void (*on_key)(char c);     /* key dispatch */
    void (*on_tick)(void);      /* periodic callback */
    void (*on_close)(void);     /* cleanup on terminal close or ESC */
    int tick_interval;          /* PIT ticks between on_tick (0=disabled) */
    int task_id;                /* for CPU tracking */
} shell_fg_app_t;

void             shell_register_fg_app(shell_fg_app_t *app);
void             shell_unregister_fg_app(void);
shell_fg_app_t  *shell_get_fg_app(void);

/* Desktop terminal integration */
extern int shell_exit_requested;

#endif
