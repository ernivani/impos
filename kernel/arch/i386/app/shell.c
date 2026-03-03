#include <kernel/shell.h>
#include <kernel/shell_cmd.h>
#include <kernel/fs.h>
#include <kernel/tty.h>
#include <kernel/config.h>
#include <kernel/net.h>
#include <kernel/env.h>
#include <kernel/user.h>
#include <kernel/group.h>
#include <kernel/hostname.h>
#include <kernel/quota.h>
#include <kernel/io.h>
#include <kernel/multiboot.h>
#include <kernel/task.h>
#include <kernel/sched.h>
#include <kernel/sh_parse.h>
#include <kernel/pe_loader.h>
#include <kernel/elf_loader.h>
#include <kernel/rtc.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

int shell_exit_requested = 0;

/* ── Shell pipe infrastructure ── */
#define SHELL_PIPE_BUF_SIZE 4096
char  shell_pipe_buf[SHELL_PIPE_BUF_SIZE];
int   shell_pipe_len;
int   shell_pipe_mode;  /* 1 = capturing output into pipe_buf */

/* Hook for printf redirection: called from putchar when pipe mode active */
void shell_pipe_putchar(char c) {
    if (shell_pipe_mode && shell_pipe_len < SHELL_PIPE_BUF_SIZE - 1)
        shell_pipe_buf[shell_pipe_len++] = c;
}

int shell_is_pipe_mode(void) {
    return shell_pipe_mode;
}

/* ── Pipe input buffer (for pipeline stages) ── */
static const char *shell_pipe_input = NULL;
static int shell_pipe_input_len = 0;
static int shell_pipe_input_pos = 0;

void shell_set_pipe_input(const char *buf, int len) {
    shell_pipe_input = buf;
    shell_pipe_input_len = len;
    shell_pipe_input_pos = 0;
}

int shell_read_pipe_input(char *buf, int count) {
    if (!shell_pipe_input || shell_pipe_input_pos >= shell_pipe_input_len)
        return 0;
    int avail = shell_pipe_input_len - shell_pipe_input_pos;
    int to_read = count < avail ? count : avail;
    memcpy(buf, shell_pipe_input + shell_pipe_input_pos, to_read);
    shell_pipe_input_pos += to_read;
    return to_read;
}

int shell_has_pipe_input(void) {
    return shell_pipe_input != NULL && shell_pipe_input_pos < shell_pipe_input_len;
}

int shell_pipe_input_remaining(void) {
    if (!shell_pipe_input || shell_pipe_input_pos >= shell_pipe_input_len)
        return 0;
    return shell_pipe_input_len - shell_pipe_input_pos;
}

/* ── Job control table ── */
static shell_job_t job_table[MAX_JOBS];

int shell_job_add(int pid, int tid, const char *cmd) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (job_table[i].state == JOB_NONE || job_table[i].state == JOB_DONE) {
            job_table[i].pid = pid;
            job_table[i].tid = tid;
            job_table[i].pgid = pid;
            job_table[i].state = JOB_RUNNING;
            strncpy(job_table[i].command, cmd, 127);
            job_table[i].command[127] = '\0';
            return i + 1; /* 1-indexed job number */
        }
    }
    return -1;
}

void shell_job_update_all(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (job_table[i].state == JOB_RUNNING || job_table[i].state == JOB_STOPPED) {
            task_info_t *t = task_get(job_table[i].tid);
            if (!t || !t->active || t->state == TASK_STATE_ZOMBIE) {
                job_table[i].state = JOB_DONE;
            } else if (t->state == TASK_STATE_STOPPED) {
                job_table[i].state = JOB_STOPPED;
            } else {
                job_table[i].state = JOB_RUNNING;
            }
        }
    }
}

shell_job_t *shell_get_job_table(void) {
    return job_table;
}

/* Foreground app (non-blocking command like top) */
static shell_fg_app_t *active_fg_app = NULL;

void shell_register_fg_app(shell_fg_app_t *app) { active_fg_app = app; }
void shell_unregister_fg_app(void) { active_fg_app = NULL; }
shell_fg_app_t *shell_get_fg_app(void) { return active_fg_app; }

#define MAX_ARGS 64

/* ── Shared helper: read file or pipe input ── */
char *shell_read_input(const char *filename, int *out_len) {
    if (filename) {
        uint32_t parent;
        char name[28];
        int ino = fs_resolve_path(filename, &parent, name);
        if (ino < 0) {
            printf("sh: %s: No such file\n", filename);
            return NULL;
        }
        inode_t node;
        fs_read_inode(ino, &node);
        if (node.size == 0) {
            *out_len = 0;
            return (char *)calloc(1, 1);
        }
        char *buf = (char *)malloc(node.size + 1);
        if (!buf) return NULL;
        uint32_t offset = 0;
        while (offset < node.size) {
            int n = fs_read_at(ino, (uint8_t *)buf + offset, offset, node.size - offset);
            if (n <= 0) break;
            offset += n;
        }
        buf[offset] = '\0';
        *out_len = offset;
        return buf;
    }
    /* Read from pipe input */
    if (shell_has_pipe_input()) {
        int len = shell_pipe_input_len - shell_pipe_input_pos;
        char *buf = (char *)malloc(len + 1);
        if (!buf) return NULL;
        int n = shell_read_pipe_input(buf, len);
        buf[n] = '\0';
        *out_len = n;
        return buf;
    }
    return NULL;
}

/* ── Distributed command table ── */
static command_t all_commands[128];
static int num_commands = 0;

/* Forward declarations for help/man (they reference the command table) */
static void cmd_help(int argc, char* argv[]);
static void cmd_man(int argc, char* argv[]);

static void add_group(const command_t *cmds, int count) {
    for (int i = 0; i < count && num_commands < 128; i++)
        all_commands[num_commands++] = cmds[i];
}

static void shell_build_command_table(void) {
    num_commands = 0;

    /* help and man come first */
    all_commands[num_commands++] = (command_t){
        "help", cmd_help,
        "Display information about builtin commands",
        "help: help [COMMAND]\n"
        "    Display helpful information about builtin commands.\n"
        "    If COMMAND is specified, gives detailed help on that\n"
        "    command, otherwise lists all available commands.\n",
        "NAME\n"
        "    help - display information about builtin commands\n\n"
        "SYNOPSIS\n"
        "    help [COMMAND]\n\n"
        "DESCRIPTION\n"
        "    Displays brief summaries of builtin commands. If\n"
        "    COMMAND is specified, detailed information about that\n"
        "    command is shown. Without arguments, lists all\n"
        "    available shell commands with short descriptions.\n",
        0
    };
    all_commands[num_commands++] = (command_t){
        "man", cmd_man,
        "Display manual pages",
        "man: man COMMAND\n"
        "    Display the manual page for COMMAND.\n",
        "NAME\n"
        "    man - display manual pages for commands\n\n"
        "SYNOPSIS\n"
        "    man COMMAND\n\n"
        "DESCRIPTION\n"
        "    The man utility displays the manual page for the\n"
        "    given COMMAND. Each manual page contains the command\n"
        "    name, synopsis, and a detailed description of its\n"
        "    behavior and options.\n",
        0
    };

    /* Merge all command groups */
    int count;
    add_group(cmd_fs_commands(&count), count);
    add_group(cmd_text_commands(&count), count);
    add_group(cmd_net_commands(&count), count);
    add_group(cmd_user_commands(&count), count);
    add_group(cmd_system_commands(&count), count);
    add_group(cmd_gfx_commands(&count), count);
    add_group(cmd_process_commands(&count), count);
    add_group(cmd_exec_commands(&count), count);
}

/* ── help / man (need the full command table) ── */

static void cmd_help(int argc, char* argv[]) {
    if (argc >= 2) {
        for (int i = 0; i < num_commands; i++) {
            if (strcmp(argv[1], all_commands[i].name) == 0) {
                printf("%s", all_commands[i].help_text);
                return;
            }
        }
        printf("help: no help topic for '%s'\n", argv[1]);
        return;
    }

    int is_root = (user_get_current_uid() == 0);
    printf("Available commands:\n");
    for (int i = 0; i < num_commands; i++) {
        if ((all_commands[i].flags & CMD_FLAG_ROOT) && !is_root)
            continue;
        if (all_commands[i].flags & CMD_FLAG_ROOT)
            printf("  %s [root] - %s\n", all_commands[i].name, all_commands[i].short_desc);
        else
            printf("  %s - %s\n", all_commands[i].name, all_commands[i].short_desc);
    }
}

static void cmd_man(int argc, char* argv[]) {
    if (argc < 2) {
        printf("What manual page do you want?\n");
        return;
    }

    for (int i = 0; i < num_commands; i++) {
        if (strcmp(argv[1], all_commands[i].name) == 0) {
            printf("%s", all_commands[i].man_page);
            return;
        }
    }
    printf("No manual entry for %s\n", argv[1]);
}

/* ── History ring buffer ── */
static char history_buf[SHELL_HIST_SIZE][SHELL_CMD_SIZE];
static int history_next;
static int history_count;

/* Tab completion cycling state */
static size_t last_completion_pos = 0;
static int completion_cycle_index = 0;
static char completion_matches[32][64] = {{0}};
static int completion_matches_count = 0;
static char last_completed_word[64] = {0};

void shell_history_add(const char *cmd) {
    if (cmd == NULL || cmd[0] == '\0')
        return;
    if (history_count > 0) {
        int last = (history_next + SHELL_HIST_SIZE - 1) % SHELL_HIST_SIZE;
        if (strcmp(history_buf[last], cmd) == 0)
            return;
    }
    size_t n = SHELL_CMD_SIZE - 1;
    size_t i = 0;
    for (; i < n && cmd[i]; i++)
        history_buf[history_next][i] = cmd[i];
    history_buf[history_next][i] = '\0';
    history_next = (history_next + 1) % SHELL_HIST_SIZE;
    if (history_count < SHELL_HIST_SIZE)
        history_count++;
}

int shell_history_count(void) {
    return history_count;
}

const char *shell_history_entry(int index) {
    if (index < 0 || index >= history_count)
        return NULL;
    int slot = (history_next - history_count + index + SHELL_HIST_SIZE) % SHELL_HIST_SIZE;
    return history_buf[slot];
}

/* ── Login ── */

int shell_login(void) {
    printf("\n");
    printf("ImposOS Login\n");
    printf("\n");

    while (1) {
        printf("Username: ");
        char username[64];
        size_t username_len = 0;

        while (username_len < sizeof(username) - 1) {
            int c = getchar();
            if (c == '\n' || c == '\r') {
                break;
            } else if (c == '\b' || c == 127) {
                if (username_len > 0) {
                    username_len--;
                    printf("\b \b");
                }
            } else if (c >= 32 && c < 127) {
                username[username_len++] = c;
                putchar(c);
            }
        }
        username[username_len] = '\0';
        printf("\n");

        if (username[0] == '\0') {
            continue;
        }

        printf("Password: ");
        char password[64];
        size_t password_len = 0;

        while (password_len < sizeof(password) - 1) {
            int c = getchar();
            if (c == '\n' || c == '\r') {
                break;
            } else if (c == '\b' || c == 127) {
                if (password_len > 0) {
                    password_len--;
                    printf("\b \b");
                }
            } else if (c >= 32 && c < 127) {
                password[password_len++] = c;
                putchar('*');
            }
        }
        password[password_len] = '\0';
        printf("\n");

        user_t* authenticated = user_authenticate(username, password);
        if (authenticated) {
            user_set_current(authenticated->username);
            fs_change_directory(authenticated->home);
            printf("Welcome, %s!\n\n", authenticated->username);
            return 0;
        } else {
            printf("Login incorrect\n\n");
        }
    }
}

/* ── Subsystem initialization ── */

void shell_initialize_subsystems(void) {
    DBG("subsys: fs_initialize");    fs_initialize();
    DBG("subsys: initrd");
    if (initrd_data && initrd_size > 0) { fs_mount_initrd(initrd_data, initrd_size); }
    DBG("subsys: config"); config_initialize();
    DBG("subsys: rtc");    rtc_init();
    DBG("subsys: net");    net_initialize();
    DBG("subsys: env");    env_initialize();
    DBG("subsys: host");   hostname_initialize();
    DBG("subsys: user");   user_initialize();
    DBG("subsys: group");  group_initialize();
    DBG("subsys: quota");  quota_initialize();

    /* Ensure /dev/dri exists (device nodes aren't persisted on disk) */
    {
        uint32_t parent;
        char name[28];
        if (fs_resolve_path("/dev/dri", &parent, name) < 0) {
            fs_create_file("/dev/dri", 1);
        }
        if (fs_resolve_path("/dev/dri/card0", &parent, name) < 0) {
            fs_create_device("/dev/dri/card0", DEV_MAJOR_DRM, 0);
        }
    }

    DBG("subsys: done");
}

int shell_needs_setup(void) {
    return !user_system_initialized();
}

void shell_initialize(void) {
    shell_initialize_subsystems();

    /* Build the distributed command table */
    shell_build_command_table();

    printf("ImposOS Shell v2.0\n");

    /* Check if system needs initial setup */
    if (!user_system_initialized()) {
        printf("\n");
        printf("=== ImposOS Initial Setup ===\n");
        printf("No users found. Let's create the administrator account.\n");
        printf("\n");

        /* Ask for hostname */
        printf("Enter hostname (or press Enter for 'imposos'): ");
        char hostname[64];
        size_t hostname_len = 0;

        while (hostname_len < sizeof(hostname) - 1) {
            int c = getchar();
            if (c == '\n' || c == '\r') {
                break;
            } else if (c == '\b' || c == 127) {
                if (hostname_len > 0) {
                    hostname_len--;
                    printf("\b \b");
                }
            } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') {
                hostname[hostname_len++] = c;
                putchar(c);
            } else if (c >= 'A' && c <= 'Z') {
                hostname[hostname_len++] = c + 32;  /* Lowercase */
                putchar(c + 32);
            }
        }
        hostname[hostname_len] = '\0';
        printf("\n");

        if (hostname[0] == '\0') {
            strcpy(hostname, "imposos");
        }

        hostname_set(hostname);
        hostname_save();
        printf("Hostname set to: %s\n", hostname_get());
        printf("\n");

        /* Create root user */
        printf("Creating root account...\n");
        printf("Enter password for root: ");
        char root_password[64];
        size_t root_pass_len = 0;

        /* Read root password */
        while (root_pass_len < sizeof(root_password) - 1) {
            int c = getchar();
            if (c == '\n' || c == '\r') {
                break;
            } else if (c == '\b' || c == 127) {
                if (root_pass_len > 0) {
                    root_pass_len--;
                    printf("\b \b");
                }
            } else if (c >= 32 && c < 127) {
                root_password[root_pass_len++] = c;
                putchar('*');
            }
        }
        root_password[root_pass_len] = '\0';
        printf("\n");

        /* Create root home directory */
        fs_create_file("/home", 1);
        fs_create_file("/home/root", 1);

        user_create("root", root_password, "/home/root", 0, 0);
        printf("Root account created!\n");
        printf("\n");

        /* Create regular user */
        printf("Now let's create your user account.\n");
        printf("Enter username: ");
        char username[32];
        size_t username_len = 0;

        while (username_len < sizeof(username) - 1) {
            int c = getchar();
            if (c == '\n' || c == '\r') {
                break;
            } else if (c == '\b' || c == 127) {
                if (username_len > 0) {
                    username_len--;
                    printf("\b \b");
                }
            } else if (c >= 'a' && c <= 'z') {
                username[username_len++] = c;
                putchar(c);
            } else if (c >= 'A' && c <= 'Z') {
                username[username_len++] = c + 32;  /* Lowercase */
                putchar(c + 32);
            } else if ((c >= '0' && c <= '9') && username_len > 0) {
                username[username_len++] = c;
                putchar(c);
            }
        }
        username[username_len] = '\0';
        printf("\n");

        if (username[0] == '\0') {
            strcpy(username, "user");
            printf("Using default username: user\n");
        }

        printf("Enter password for %s: ", username);
        char user_password[64];
        size_t user_pass_len = 0;

        while (user_pass_len < sizeof(user_password) - 1) {
            int c = getchar();
            if (c == '\n' || c == '\r') {
                break;
            } else if (c == '\b' || c == 127) {
                if (user_pass_len > 0) {
                    user_pass_len--;
                    printf("\b \b");
                }
            } else if (c >= 32 && c < 127) {
                user_password[user_pass_len++] = c;
                putchar('*');
            }
        }
        user_password[user_pass_len] = '\0';
        printf("\n");

        /* Create user home directory */
        char user_home[128];
        snprintf(user_home, sizeof(user_home), "/home/%s", username);
        fs_create_file(user_home, 1);

        user_create(username, user_password, user_home, 1000, 1000);
        printf("User '%s' created!\n", username);
        printf("\n");

        /* Save users to disk */
        user_save();
        fs_sync();

        printf("Setup complete! Logging in as %s...\n", username);
        user_set_current(username);
        fs_change_directory(user_home);
        printf("\n");
    } else {
        /* System already initialized - prompt for login */
        shell_login();
    }

    printf("Type 'help' for a list of commands.\n");
    printf("Press Tab for smart auto-completion (commands, options, files).\n");
}

/* ── Tab completion ── */

size_t shell_autocomplete(char* buffer, size_t buffer_pos, size_t buffer_size) {
    if (buffer_pos >= buffer_size) {
        return buffer_pos;
    }

    // Find the start of the current word (after the last space)
    size_t start = buffer_pos;
    while (start > 0 && buffer[start - 1] != ' ') {
        start--;
    }

    size_t prefix_len = buffer_pos - start;

    // Check if we're continuing a previous completion cycle
    int is_continuing_cycle = 0;
    if (last_completion_pos == start && completion_matches_count > 0) {
        char current_word[64] = {0};
        size_t word_len = buffer_pos - start;
        if (word_len < sizeof(current_word)) {
            memcpy(current_word, buffer + start, word_len);
            current_word[word_len] = '\0';

            // Check if current word matches ANY of our completion matches
            for (int i = 0; i < completion_matches_count; i++) {
                if (strcmp(current_word, completion_matches[i]) == 0) {
                    is_continuing_cycle = 1;
                    break;
                }
            }
        }
    }

    // If not continuing, reset cycle index
    if (!is_continuing_cycle) {
        completion_cycle_index = 0;
    }

    // Parse the command line to understand context
    int word_count = 0;
    size_t word_starts[10];
    int in_word = 0;

    for (size_t i = 0; i < buffer_pos; i++) {
        if (buffer[i] != ' ' && !in_word) {
            if (word_count < 10) {
                word_starts[word_count] = i;
            }
            word_count++;
            in_word = 1;
        } else if (buffer[i] == ' ') {
            in_word = 0;
        }
    }

    if (buffer_pos > 0 && buffer[buffer_pos - 1] == ' ') {
        if (word_count < 10) {
            word_starts[word_count] = buffer_pos;
        }
        word_count++;
    }

    // Extract command name
    char cmd_name[32] = {0};
    if (word_count > 0) {
        size_t cmd_start = word_starts[0];
        size_t cmd_end = cmd_start;
        while (cmd_end < buffer_pos && buffer[cmd_end] != ' ') cmd_end++;
        size_t cmd_len = cmd_end - cmd_start;
        if (cmd_len >= sizeof(cmd_name)) cmd_len = sizeof(cmd_name) - 1;
        for (size_t i = 0; i < cmd_len; i++) {
            cmd_name[i] = buffer[cmd_start + i];
        }
    }

    // Collect all matches if NOT continuing a cycle
    if (!is_continuing_cycle) {
        completion_matches_count = 0;

        if (word_count == 1 && prefix_len > 0) {
            // Complete command names (skip root-only for non-root users)
            int is_root = (user_get_current_uid() == 0);
            for (int i = 0; i < num_commands && completion_matches_count < 32; i++) {
                if ((all_commands[i].flags & CMD_FLAG_ROOT) && !is_root)
                    continue;
                const char* name = all_commands[i].name;
                size_t j = 0;
                for (; j < prefix_len; j++) {
                    if (name[j] == '\0' || name[j] != buffer[start + j]) {
                        break;
                    }
                }
                if (j == prefix_len) {
                    strcpy(completion_matches[completion_matches_count], name);
                    completion_matches_count++;
                }
            }
        } else if (word_count >= 2) {
            if (strcmp(cmd_name, "timedatectl") == 0 && word_count == 2) {
                const char* timedatectl_cmds[] = {
                    "status", "set-time", "set-date", "set-timezone", "list-timezones"
                };
                for (size_t i = 0; i < 5 && completion_matches_count < 32; i++) {
                    const char* subcmd = timedatectl_cmds[i];
                    size_t j = 0;
                    for (; j < prefix_len; j++) {
                        if (subcmd[j] == '\0' || subcmd[j] != buffer[start + j]) {
                            break;
                        }
                    }
                    if (j == prefix_len) {
                        strcpy(completion_matches[completion_matches_count], subcmd);
                        completion_matches_count++;
                    }
                }
            } else if (strcmp(cmd_name, "setlayout") == 0 && word_count == 2) {
                const char* layouts[] = { "fr", "us" };
                for (size_t i = 0; i < 2 && completion_matches_count < 32; i++) {
                    const char* layout = layouts[i];
                    size_t j = 0;
                    for (; j < prefix_len; j++) {
                        if (layout[j] == '\0' || layout[j] != buffer[start + j]) {
                            break;
                        }
                    }
                    if (j == prefix_len) {
                        strcpy(completion_matches[completion_matches_count], layout);
                        completion_matches_count++;
                    }
                }
            } else if (strcmp(cmd_name, "ifconfig") == 0 && word_count == 2) {
                const char* ifaces[] = { "eth0" };
                for (size_t i = 0; i < 1 && completion_matches_count < 32; i++) {
                    const char* iface = ifaces[i];
                    size_t j = 0;
                    for (; j < prefix_len; j++) {
                        if (iface[j] == '\0' || iface[j] != buffer[start + j]) {
                            break;
                        }
                    }
                    if (j == prefix_len) {
                        strcpy(completion_matches[completion_matches_count], iface);
                        completion_matches_count++;
                    }
                }
            } else if (strcmp(cmd_name, "ifconfig") == 0 && word_count == 3) {
                const char* opts[] = { "up", "down" };
                for (size_t i = 0; i < 2 && completion_matches_count < 32; i++) {
                    const char* opt = opts[i];
                    size_t j = 0;
                    for (; j < prefix_len; j++) {
                        if (opt[j] == '\0' || opt[j] != buffer[start + j]) {
                            break;
                        }
                    }
                    if (j == prefix_len) {
                        strcpy(completion_matches[completion_matches_count], opt);
                        completion_matches_count++;
                    }
                }
            } else if (strcmp(cmd_name, "ls") == 0 && prefix_len > 0 && buffer[start] == '-') {
                const char* options[] = { "-a", "-l", "-la", "-al" };
                for (size_t i = 0; i < 4 && completion_matches_count < 32; i++) {
                    const char* opt = options[i];
                    size_t j = 0;
                    for (; j < prefix_len; j++) {
                        if (opt[j] == '\0' || opt[j] != buffer[start + j]) {
                            break;
                        }
                    }
                    if (j == prefix_len) {
                        strcpy(completion_matches[completion_matches_count], opt);
                        completion_matches_count++;
                    }
                }
            } else {
                // Complete filenames
                // Extract the current word being completed
                char word[256] = {0};
                size_t word_len = buffer_pos - start;
                if (word_len < sizeof(word)) {
                    memcpy(word, buffer + start, word_len);
                }


                // Find the last / in the word to separate directory from filename
                char dir_path[256] = {0};
                char file_prefix[256] = {0};
                int found_slash = 0;
                size_t last_slash = 0;

                for (size_t i = word_len; i > 0; i--) {
                    if (word[i-1] == '/') {
                        last_slash = i;
                        found_slash = 1;
                        break;
                    }
                }

                // Extract directory path and filename prefix
                if (found_slash) {
                    // Has a / - split into dir + prefix
                    memcpy(dir_path, word, last_slash);
                    dir_path[last_slash] = '\0';
                    if (last_slash < word_len) {
                        memcpy(file_prefix, word + last_slash, word_len - last_slash);
                        file_prefix[word_len - last_slash] = '\0';
                    }
                } else {
                    // No / - use current directory
                    strcpy(file_prefix, word);
                }

                size_t file_prefix_len = strlen(file_prefix);

                // Get the target directory inode
                uint32_t target_inode;
                if (dir_path[0] != '\0') {
                    // Save current directory
                    uint32_t saved_cwd = fs_get_cwd_inode();

                    // Try to navigate to target directory
                    if (fs_change_directory(dir_path) != 0) {
                        // Path doesn't exist
                        fs_change_directory_by_inode(saved_cwd);
                        goto skip_file_completion;
                    }
                    target_inode = fs_get_cwd_inode();

                    // Restore original directory
                    fs_change_directory_by_inode(saved_cwd);
                } else {
                    target_inode = fs_get_cwd_inode();
                }

                // Read target directory
                inode_t dir_inode;
                if (fs_read_inode(target_inode, &dir_inode) == 0 && dir_inode.type == INODE_DIR) {
                    /* Process one block at a time to avoid 32KB stack allocation */
                    static uint8_t ac_block[BLOCK_SIZE];
                    int epb = BLOCK_SIZE / (int)sizeof(dir_entry_t);
                    for (uint8_t bi = 0; bi < dir_inode.num_blocks && completion_matches_count < 32; bi++) {
                        if (fs_read_block(dir_inode.blocks[bi], ac_block) != 0) break;
                        dir_entry_t* entries = (dir_entry_t*)ac_block;

                        for (int ei = 0; ei < epb && completion_matches_count < 32; ei++) {
                            const char* name = entries[ei].name;
                            if (name[0] == '\0') continue;
                            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

                            size_t j = 0;
                            for (; j < file_prefix_len; j++) {
                                if (name[j] == '\0' || name[j] != file_prefix[j]) break;
                            }
                            if (j == file_prefix_len) {
                                if (dir_path[0] != '\0') {
                                    strcpy(completion_matches[completion_matches_count], dir_path);
                                    strcat(completion_matches[completion_matches_count], name);
                                } else {
                                    strcpy(completion_matches[completion_matches_count], name);
                                }
                                completion_matches_count++;
                            }
                        }
                    }
                }

                skip_file_completion:
                (void)0;
            }
        }

        // Save state for next completion
        last_completion_pos = start;
    }

    // No matches at all
    if (completion_matches_count == 0) {
        return buffer_pos;
    }

    // Get current match
    const char* match = completion_matches[completion_cycle_index];

    // Replace current word with match
    size_t match_len = strlen(match);

    // Clear the current word
    buffer_pos = start;

    // Copy the match
    size_t to_copy = match_len;
    if (buffer_pos + to_copy >= buffer_size) {
        to_copy = buffer_size - buffer_pos - 1;
    }

    memcpy(&buffer[buffer_pos], match, to_copy);
    buffer_pos += to_copy;

    // Save this completed word for next time
    strcpy(last_completed_word, match);

    // Move to next match for next Tab press
    completion_cycle_index = (completion_cycle_index + 1) % completion_matches_count;

    return buffer_pos;
}

/* ── Command dispatch ── */

int shell_dispatch_command(int argc, char *argv[]) {
    if (argc == 0) return 0;

    /* Check for variable assignment: FOO=bar with no command */
    if (argc == 1 && strchr(argv[0], '=') && argv[0][0] != '=') {
        char *eq = strchr(argv[0], '=');
        char name[64];
        int nlen = eq - argv[0];
        if (nlen > 63) nlen = 63;
        memcpy(name, argv[0], nlen);
        name[nlen] = '\0';
        env_set(name, eq + 1);
        return 0;
    }

    /* Look up builtin */
    for (int i = 0; i < num_commands; i++) {
        if (strcmp(argv[0], all_commands[i].name) == 0) {
            if ((all_commands[i].flags & CMD_FLAG_ROOT) && user_get_current_uid() != 0) {
                printf("%s: permission denied (requires root)\n", argv[0]);
                return 1;
            }
            all_commands[i].func(argc, argv);
            return sh_get_exit_code();
        }
    }

    /* Try to run as an executable via ELF or PE loader */
    {
        const char *name = argv[0];
        size_t nlen = strlen(name);

        /* Try ELF: exact path first, then /bin/<name> */
        int ret = elf_run_argv(name, argc, (const char **)argv);
        if (ret >= 0) {
            task_info_t *t = task_get(ret);
            while (t && t->active && t->state != TASK_STATE_ZOMBIE)
                task_yield();
            if (t) sh_set_exit_code(t->exit_code);
            return sh_get_exit_code();
        }

        char path_buf[64];
        snprintf(path_buf, sizeof(path_buf), "/bin/%s", name);
        ret = elf_run_argv(path_buf, argc, (const char **)argv);
        if (ret >= 0) {
            task_info_t *t = task_get(ret);
            while (t && t->active && t->state != TASK_STATE_ZOMBIE)
                task_yield();
            if (t) sh_set_exit_code(t->exit_code);
            return sh_get_exit_code();
        }

        /* Try PE: append .exe if needed */
        char exe_name[64];
        if (nlen > 4 && strcmp(name + nlen - 4, ".exe") == 0) {
            strncpy(exe_name, name, sizeof(exe_name) - 1);
            exe_name[sizeof(exe_name) - 1] = '\0';
        } else {
            snprintf(exe_name, sizeof(exe_name), "%s.exe", name);
        }

        ret = pe_run(exe_name);
        if (ret >= 0) {
            for (int i = 0; i < 5; i++) task_yield();
            return 0;
        }
    }

    printf("%s: command not found\n", argv[0]);
    return 127;
}

void shell_process_command(char* command) {
    if (!command || !command[0])
        return;

    /* Tokenize using the new parser */
    sh_token_t tokens[SH_MAX_TOKENS];
    int count = sh_tokenize(command, tokens, SH_MAX_TOKENS);
    if (count <= 0)
        return;

    /* Parse and execute via AST */
    sh_run(tokens, count);
}
