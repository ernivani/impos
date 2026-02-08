#include <kernel/shell.h>
#include <kernel/fs.h>
#include <kernel/tty.h>
#include <kernel/vi.h>
#include <kernel/config.h>
#include <kernel/net.h>
#include <kernel/pci.h>
#include <kernel/ip.h>
#include <kernel/env.h>
#include <kernel/user.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define MAX_ARGS 64

typedef void (*cmd_func_t)(int argc, char* argv[]);

typedef struct {
    const char* name;
    cmd_func_t func;
    const char* short_desc;
    const char* help_text;
    const char* man_page;
} command_t;

static void cmd_echo(int argc, char* argv[]);
static void cmd_cat(int argc, char* argv[]);
static void cmd_ls(int argc, char* argv[]);
static void cmd_cd(int argc, char* argv[]);
static void cmd_touch(int argc, char* argv[]);
static void cmd_help(int argc, char* argv[]);
static void cmd_man(int argc, char* argv[]);
static void cmd_clear(int argc, char* argv[]);
static void cmd_history(int argc, char* argv[]);
static void cmd_pwd(int argc, char* argv[]);
static void cmd_mkdir(int argc, char* argv[]);
static void cmd_rm(int argc, char* argv[]);
static void cmd_vi(int argc, char* argv[]);
static void cmd_setlayout(int argc, char* argv[]);
static void cmd_sync(int argc, char* argv[]);
static void cmd_exit(int argc, char* argv[]);
static void cmd_shutdown(int argc, char* argv[]);
static void cmd_timedatectl(int argc, char* argv[]);
static void cmd_ifconfig(int argc, char* argv[]);
static void cmd_ping(int argc, char* argv[]);
static void cmd_lspci(int argc, char* argv[]);
static void cmd_arp(int argc, char* argv[]);
static void cmd_export(int argc, char* argv[]);
static void cmd_env(int argc, char* argv[]);
static void cmd_whoami(int argc, char* argv[]);

static command_t commands[] = {
    {
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
        "    available shell commands with short descriptions.\n"
    },
    {
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
        "    behavior and options.\n"
    },
    {
        "echo", cmd_echo,
        "Write arguments to the standard output",
        "echo: echo [ARG ...]\n"
        "    Display the ARGs, separated by a single space,\n"
        "    followed by a newline.\n",
        "NAME\n"
        "    echo - write arguments to the standard output\n\n"
        "SYNOPSIS\n"
        "    echo [ARG ...]\n\n"
        "DESCRIPTION\n"
        "    The echo utility writes its arguments to standard\n"
        "    output, separated by single blank characters, followed\n"
        "    by a newline. If there are no arguments, only the\n"
        "    newline is written.\n"
    },
    {
        "cat", cmd_cat,
        "Display file contents",
        "cat: cat FILE\n"
        "    Read FILE and print its contents to standard output.\n",
        "NAME\n"
        "    cat - concatenate and print files\n\n"
        "SYNOPSIS\n"
        "    cat FILE\n\n"
        "DESCRIPTION\n"
        "    The cat utility reads the given FILE and writes its\n"
        "    contents to standard output. If the file does not\n"
        "    exist or is a directory, an error message is printed.\n"
    },
    {
        "ls", cmd_ls,
        "List directory contents",
        "ls: ls [-la]\n"
        "    List the contents of the current directory.\n"
        "    -a  Include entries starting with . (. and ..)\n"
        "    -l  Use long listing format\n",
        "NAME\n"
        "    ls - list directory contents\n\n"
        "SYNOPSIS\n"
        "    ls [-la]\n\n"
        "DESCRIPTION\n"
        "    For each entry in the current directory, ls prints\n"
        "    the name. By default, . and .. are hidden.\n\n"
        "OPTIONS\n"
        "    -a  Do not ignore entries starting with .\n"
        "        Shows the . (current) and .. (parent) dirs.\n\n"
        "    -l  Use a long listing format. Each entry shows\n"
        "        permissions, owner, group, size, and name.\n\n"
        "    Flags may be combined: ls -la\n"
    },
    {
        "cd", cmd_cd,
        "Change the working directory",
        "cd: cd [DIR]\n"
        "    Change the current working directory to DIR.\n"
        "    Supports absolute paths, relative paths, . and ..\n",
        "NAME\n"
        "    cd - change the working directory\n\n"
        "SYNOPSIS\n"
        "    cd [DIR]\n\n"
        "DESCRIPTION\n"
        "    Change the current working directory to DIR. If DIR\n"
        "    begins with / it is treated as an absolute path,\n"
        "    otherwise it is relative to the current directory.\n"
        "    The special names . and .. refer to the current and\n"
        "    parent directory respectively.\n"
    },
    {
        "touch", cmd_touch,
        "Create an empty file",
        "touch: touch FILE\n"
        "    Create an empty file named FILE.\n",
        "NAME\n"
        "    touch - create an empty file\n\n"
        "SYNOPSIS\n"
        "    touch FILE\n\n"
        "DESCRIPTION\n"
        "    Create an empty file named FILE in the current\n"
        "    directory. If the file already exists, an error is\n"
        "    printed. The file is created with zero size.\n"
    },
    {
        "clear", cmd_clear,
        "Clear the terminal screen",
        "clear: clear\n"
        "    Clear the terminal screen and move cursor to top.\n",
        "NAME\n"
        "    clear - clear the terminal screen\n\n"
        "SYNOPSIS\n"
        "    clear\n\n"
        "DESCRIPTION\n"
        "    Clears the VGA text-mode terminal screen and resets\n"
        "    the cursor position to row 0, column 0.\n"
    },
    {
        "pwd", cmd_pwd,
        "Print the current working directory",
        "pwd: pwd\n"
        "    Print the full pathname of the current directory.\n",
        "NAME\n"
        "    pwd - print name of current/working directory\n\n"
        "SYNOPSIS\n"
        "    pwd\n\n"
        "DESCRIPTION\n"
        "    Print the full pathname of the current working\n"
        "    directory by walking the .. chain up to /.\n"
    },
    {
        "history", cmd_history,
        "Display command history",
        "history: history\n"
        "    List previously entered commands.\n",
        "NAME\n"
        "    history - display command history\n\n"
        "SYNOPSIS\n"
        "    history\n\n"
        "DESCRIPTION\n"
        "    Prints the list of saved commands (up to 16 entries).\n"
        "    Use Up/Down in the shell to recall history.\n"
    },
    {
        "mkdir", cmd_mkdir,
        "Create a directory",
        "mkdir: mkdir NAME\n"
        "    Create a directory named NAME.\n",
        "NAME\n"
        "    mkdir - make directories\n\n"
        "SYNOPSIS\n"
        "    mkdir NAME\n\n"
        "DESCRIPTION\n"
        "    Create the directory NAME in the current working\n"
        "    directory. The new directory will contain the\n"
        "    standard . and .. entries. An error is reported if\n"
        "    NAME already exists.\n"
    },
    {
        "rm", cmd_rm,
        "Remove a file or empty directory",
        "rm: rm NAME\n"
        "    Remove the file or empty directory named NAME.\n",
        "NAME\n"
        "    rm - remove files or directories\n\n"
        "SYNOPSIS\n"
        "    rm NAME\n\n"
        "DESCRIPTION\n"
        "    Remove the file or directory named NAME. If NAME\n"
        "    is a directory it must be empty (contain only . and\n"
        "    ..). The root directory cannot be removed.\n"
    },
    {
        "vi", cmd_vi,
        "Edit a file with the vi text editor",
        "vi: vi FILE\n"
        "    Open FILE in the vi text editor.\n"
        "    Creates the file on save if it does not exist.\n",
        "NAME\n"
        "    vi - screen-oriented text editor\n\n"
        "SYNOPSIS\n"
        "    vi FILE\n\n"
        "DESCRIPTION\n"
        "    vi is a modal text editor. It starts in NORMAL mode.\n\n"
        "NORMAL MODE\n"
        "    h/Left    Move cursor left\n"
        "    j/Down    Move cursor down\n"
        "    k/Up      Move cursor up\n"
        "    l/Right   Move cursor right\n"
        "    0         Go to beginning of line\n"
        "    $         Go to end of line\n"
        "    w         Next word\n"
        "    b         Previous word\n"
        "    gg        Go to first line\n"
        "    G         Go to last line\n"
        "    i         Insert before cursor\n"
        "    a         Insert after cursor\n"
        "    A         Insert at end of line\n"
        "    o         Open line below\n"
        "    O         Open line above\n"
        "    x         Delete character\n"
        "    dd        Delete line\n"
        "    :         Enter command mode\n\n"
        "INSERT MODE\n"
        "    Type text normally. ESC returns to normal.\n\n"
        "COMMANDS\n"
        "    :w        Save file\n"
        "    :q        Quit (fails if unsaved changes)\n"
        "    :wq       Save and quit\n"
        "    :q!       Quit without saving\n"
    },
    {
        "setlayout", cmd_setlayout,
        "Set keyboard layout (fr/us)",
        "setlayout: setlayout LAYOUT\n"
        "    Set the keyboard layout. LAYOUT is 'fr' or 'us'.\n"
        "    Without arguments, shows the current layout.\n",
        "NAME\n"
        "    setlayout - change keyboard layout\n\n"
        "SYNOPSIS\n"
        "    setlayout [fr|us]\n\n"
        "DESCRIPTION\n"
        "    Changes the active keyboard layout.\n"
        "    Supported layouts:\n"
        "      fr  - French AZERTY\n"
        "      us  - US QWERTY\n\n"
        "    Without arguments, prints the current layout.\n"
    },
    {
        "sync", cmd_sync,
        "Synchronize filesystem to disk",
        "sync: sync\n"
        "    Write all cached filesystem data to disk.\n",
        "NAME\n"
        "    sync - synchronize cached writes to persistent storage\n\n"
        "SYNOPSIS\n"
        "    sync\n\n"
        "DESCRIPTION\n"
        "    Forces all modified filesystem data to be written\n"
        "    to disk immediately. This ensures data persistence\n"
        "    across reboots. The filesystem is automatically\n"
        "    synced on changes when a disk is available, but\n"
        "    this command forces an immediate sync.\n"
    },
    {
        "exit", cmd_exit,
        "Exit the shell and halt the CPU",
        "exit: exit [STATUS]\n"
        "    Exit the shell and halt the CPU.\n"
        "    STATUS defaults to 0 (success).\n",
        "NAME\n"
        "    exit - cause normal process termination\n\n"
        "SYNOPSIS\n"
        "    exit [STATUS]\n\n"
        "DESCRIPTION\n"
        "    Terminates the shell and halts the CPU. The\n"
        "    machine remains powered on but stops executing.\n"
        "    On a VM, the display stays visible.\n"
        "    Use 'shutdown' to power off the machine.\n\n"
        "    If STATUS is given, it is used as the exit code.\n"
        "    0 indicates success, nonzero indicates failure.\n"
    },
    {
        "shutdown", cmd_shutdown,
        "Power off the machine",
        "shutdown: shutdown\n"
        "    Power off the machine via ACPI.\n",
        "NAME\n"
        "    shutdown - power off the machine\n\n"
        "SYNOPSIS\n"
        "    shutdown\n\n"
        "DESCRIPTION\n"
        "    Powers off the machine using ACPI. On QEMU or\n"
        "    Bochs, the VM window closes. On real hardware\n"
        "    with ACPI support, the machine powers off.\n"
        "    If ACPI is not available, falls back to halting\n"
        "    the CPU (same as 'exit').\n"
    },
    {
        "timedatectl", cmd_timedatectl,
        "Control system time and date settings",
        "timedatectl: timedatectl [COMMAND]\n"
        "    Control and query system time and date settings.\n"
        "    Available commands:\n"
        "      status              Show current time and date settings\n"
        "      set-time TIME       Set system time (HH:MM:SS)\n"
        "      set-date DATE       Set system date (YYYY-MM-DD)\n"
        "      set-timezone TZ     Set system timezone\n"
        "      list-timezones      List available timezones\n",
        "NAME\n"
        "    timedatectl - control system time and date\n\n"
        "SYNOPSIS\n"
        "    timedatectl [COMMAND] [ARGS...]\n\n"
        "DESCRIPTION\n"
        "    Query and change system time and date settings.\n\n"
        "COMMANDS\n"
        "    status\n"
        "        Show current time, date, timezone, and uptime.\n\n"
        "    set-time TIME\n"
        "        Set the system time. TIME format: HH:MM:SS\n"
        "        Example: timedatectl set-time 14:30:00\n\n"
        "    set-date DATE\n"
        "        Set the system date. DATE format: YYYY-MM-DD\n"
        "        Example: timedatectl set-date 2026-02-07\n\n"
        "    set-timezone TIMEZONE\n"
        "        Set the system timezone.\n"
        "        Example: timedatectl set-timezone Europe/Paris\n\n"
        "    list-timezones\n"
        "        List common available timezones.\n"
    },
    {
        "ifconfig", cmd_ifconfig,
        "Configure network interface parameters",
        "ifconfig: ifconfig [interface] [options]\n"
        "    Display or configure network interface parameters.\n"
        "    Without arguments, shows current network configuration.\n"
        "    Options:\n"
        "      up                  Enable the network interface\n"
        "      down                Disable the network interface\n"
        "      IP NETMASK          Set IP address and netmask\n",
        "NAME\n"
        "    ifconfig - configure network interface\n\n"
        "SYNOPSIS\n"
        "    ifconfig [interface] [options]\n\n"
        "DESCRIPTION\n"
        "    Configure network interface parameters or display\n"
        "    current network configuration.\n\n"
        "EXAMPLES\n"
        "    ifconfig\n"
        "        Show current network configuration\n\n"
        "    ifconfig eth0 10.0.2.15 255.255.255.0\n"
        "        Set IP address and netmask\n\n"
        "    ifconfig eth0 up\n"
        "        Enable network interface\n"
    },
    {
        "ping", cmd_ping,
        "Send ICMP ECHO_REQUEST to network hosts",
        "ping: ping HOST\n"
        "    Send ICMP ECHO_REQUEST packets to HOST.\n",
        "NAME\n"
        "    ping - send ICMP ECHO_REQUEST to network hosts\n\n"
        "SYNOPSIS\n"
        "    ping HOST\n\n"
        "DESCRIPTION\n"
        "    Send ICMP ECHO_REQUEST packets to HOST and wait\n"
        "    for ECHO_RESPONSE. This is useful for testing\n"
        "    network connectivity.\n\n"
        "EXAMPLES\n"
        "    ping 10.0.2.2\n"
        "        Ping the default gateway\n"
    },
    {
        "lspci", cmd_lspci,
        "List all PCI devices",
        "lspci: lspci\n"
        "    List all PCI devices on the system.\n",
        "NAME\n"
        "    lspci - list PCI devices\n\n"
        "SYNOPSIS\n"
        "    lspci\n\n"
        "DESCRIPTION\n"
        "    Scans the PCI bus and displays information about\n"
        "    all detected PCI devices, including vendor ID,\n"
        "    device ID, and device class.\n"
    },
    {
        "arp", cmd_arp,
        "Test ARP request/reply",
        "arp: arp IP\n"
        "    Send ARP request and wait for reply.\n",
        "NAME\n"
        "    arp - test ARP protocol\n\n"
        "SYNOPSIS\n"
        "    arp IP\n\n"
        "DESCRIPTION\n"
        "    Sends an ARP request for the given IP address\n"
        "    and displays the MAC address in the reply.\n"
        "    This tests if network RX actually works.\n"
    },
    {
        "export", cmd_export,
        "Set environment variable",
        "export: export VAR=value\n"
        "    Set an environment variable.\n",
        "NAME\n"
        "    export - set environment variable\n\n"
        "SYNOPSIS\n"
        "    export VAR=value\n\n"
        "DESCRIPTION\n"
        "    Sets an environment variable that persists\n"
        "    for the current shell session.\n\n"
        "EXAMPLES\n"
        "    export PS1=\"> \"\n"
        "    export HOME=/home/user\n"
    },
    {
        "env", cmd_env,
        "List environment variables",
        "env: env\n"
        "    Display all environment variables.\n",
        "NAME\n"
        "    env - list environment variables\n\n"
        "SYNOPSIS\n"
        "    env\n\n"
        "DESCRIPTION\n"
        "    Displays all currently set environment\n"
        "    variables and their values.\n"
    },
    {
        "whoami", cmd_whoami,
        "Display current user",
        "whoami: whoami\n"
        "    Display the current username.\n",
        "NAME\n"
        "    whoami - print effective userid\n\n"
        "SYNOPSIS\n"
        "    whoami\n\n"
        "DESCRIPTION\n"
        "    Prints the name of the current user.\n"
    },
};

#define NUM_COMMANDS (sizeof(commands) / sizeof(commands[0]))

/* History ring buffer: newest at (history_next - 1) % SHELL_HIST_SIZE */
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

void shell_initialize(void) {
    config_initialize();
    net_initialize();
    env_initialize();
    user_initialize();
    
    printf("ImposOS Shell v2.0\n");
    
    /* Check if system needs initial setup */
    if (!user_system_initialized()) {
        printf("\n");
        printf("=== ImposOS Initial Setup ===\n");
        printf("No users found. Let's create the administrator account.\n");
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
        /* System already initialized - auto-login as first user or root */
        user_t* first_user = user_get_by_uid(1000);
        if (!first_user) {
            first_user = user_get_by_uid(0);  /* Fallback to root */
        }
        
        if (first_user) {
            user_set_current(first_user->username);
            fs_change_directory(first_user->home);
        }
    }
    
    printf("Type 'help' for a list of commands.\n");
    printf("Press Tab for smart auto-completion (commands, options, files).\n");
}

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
            // Complete command names
            for (size_t i = 0; i < NUM_COMMANDS && completion_matches_count < 32; i++) {
                const char* name = commands[i].name;
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
            } else if (prefix_len >= 0) {
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
                    uint8_t dir_data[MAX_FILE_SIZE];
                    size_t dir_size = dir_inode.size;
                    if (dir_size > MAX_FILE_SIZE) dir_size = MAX_FILE_SIZE;

                    size_t bytes_read = 0;
                    for (uint8_t i = 0; i < dir_inode.num_blocks && bytes_read < dir_size; i++) {
                        uint8_t block_data[BLOCK_SIZE];
                        if (fs_read_block(dir_inode.blocks[i], block_data) != 0) break;
                        size_t to_copy = BLOCK_SIZE;
                        if (bytes_read + to_copy > dir_size) to_copy = dir_size - bytes_read;
                        memcpy(dir_data + bytes_read, block_data, to_copy);
                        bytes_read += to_copy;
                    }

                    size_t num_entries = dir_size / sizeof(dir_entry_t);
                    dir_entry_t* entries = (dir_entry_t*)dir_data;
                    

                    for (size_t i = 0; i < num_entries && completion_matches_count < 32; i++) {
                        const char* name = entries[i].name;
                        if (name[0] == '\0') continue;
                        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

                        // Compare with file_prefix instead of full word
                        size_t j = 0;
                        for (; j < file_prefix_len; j++) {
                            if (name[j] == '\0' || name[j] != file_prefix[j]) {
                                break;
                            }
                        }
                        if (j == file_prefix_len) {
                            // Store full path (dir_path + name)
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

void shell_process_command(char* command) {
    char* argv[MAX_ARGS];
    int argc = 0;

    char* token = strtok(command, " ");
    while (token != NULL && argc < MAX_ARGS) {
        argv[argc++] = token;
        token = strtok(NULL, " ");
    }

    if (argc == 0) return;

    for (size_t i = 0; i < NUM_COMMANDS; i++) {
        if (strcmp(argv[0], commands[i].name) == 0) {
            commands[i].func(argc, argv);
            return;
        }
    }

    printf("%s: command not found\n", argv[0]);
}

static void cmd_help(int argc, char* argv[]) {
    if (argc >= 2) {
        for (size_t i = 0; i < NUM_COMMANDS; i++) {
            if (strcmp(argv[1], commands[i].name) == 0) {
                printf("%s", commands[i].help_text);
                return;
            }
        }
        printf("help: no help topic for '%s'\n", argv[1]);
        return;
    }

    printf("Available commands:\n");
    for (size_t i = 0; i < NUM_COMMANDS; i++) {
        printf("  %s - %s\n", commands[i].name, commands[i].short_desc);
    }
}

static void cmd_man(int argc, char* argv[]) {
    if (argc < 2) {
        printf("What manual page do you want?\n");
        return;
    }

    for (size_t i = 0; i < NUM_COMMANDS; i++) {
        if (strcmp(argv[1], commands[i].name) == 0) {
            printf("%s", commands[i].man_page);
            return;
        }
    }
    printf("No manual entry for %s\n", argv[1]);
}

static void cmd_echo(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) printf(" ");
        printf("%s", argv[i]);
    }
    printf("\n");
}

static void cmd_cat(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: cat <filename>\n");
        return;
    }

    uint8_t buffer[MAX_FILE_SIZE];
    size_t size;
    if (fs_read_file(argv[1], buffer, &size) == 0) {
        for (size_t i = 0; i < size; i++) {
            putchar(buffer[i]);
        }
        printf("\n");
    } else {
        printf("cat: %s: No such file\n", argv[1]);
    }
}

static int parse_ls_flags(int argc, char* argv[]) {
    int flags = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') continue;
        for (int j = 1; argv[i][j]; j++) {
            if (argv[i][j] == 'a') flags |= LS_ALL;
            else if (argv[i][j] == 'l') flags |= LS_LONG;
        }
    }
    return flags;
}

static void cmd_ls(int argc, char* argv[]) {
    int flags = parse_ls_flags(argc, argv);
    fs_list_directory(flags);
}

static void cmd_cd(int argc, char* argv[]) {
    if (argc < 2) {
        fs_change_directory("/home/root");
        return;
    }

    if (fs_change_directory(argv[1]) != 0) {
        printf("cd: %s: No such directory\n", argv[1]);
    }
}

static void cmd_touch(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: touch <filename>\n");
        return;
    }

    if (fs_create_file(argv[1], 0) != 0) {
        printf("touch: cannot create file '%s'\n", argv[1]);
    }
}

static void cmd_clear(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    terminal_clear();
}

static void cmd_history(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    int n = shell_history_count();
    for (int i = 0; i < n; i++) {
        const char *entry = shell_history_entry(i);
        if (entry != NULL)
            printf("  %d  %s\n", i + 1, entry);
    }
}

static void cmd_pwd(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    printf("%s\n", fs_get_cwd());
}

static void cmd_mkdir(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: mkdir <name>\n");
        return;
    }

    if (fs_create_file(argv[1], 1) != 0) {
        printf("mkdir: cannot create directory '%s'\n", argv[1]);
    }
}

static void cmd_rm(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: rm <name>\n");
        return;
    }

    if (fs_delete_file(argv[1]) != 0) {
        printf("rm: cannot remove '%s'\n", argv[1]);
    }
}

static void cmd_vi(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: vi <filename>\n");
        return;
    }
    vi_open(argv[1]);
}

static void cmd_setlayout(int argc, char* argv[]) {
    if (argc < 2) {
        uint8_t layout = config_get_keyboard_layout();
        printf("Current layout: %s\n", layout == KB_LAYOUT_FR ? "fr" : "us");
        return;
    }
    if (strcmp(argv[1], "fr") == 0) {
        keyboard_set_layout(KB_LAYOUT_FR);
        config_set_keyboard_layout(KB_LAYOUT_FR);
        printf("Keyboard layout set to AZERTY (fr)\n");
    } else if (strcmp(argv[1], "us") == 0) {
        keyboard_set_layout(KB_LAYOUT_US);
        config_set_keyboard_layout(KB_LAYOUT_US);
        printf("Keyboard layout set to QWERTY (us)\n");
    } else {
        printf("Unknown layout '%s'. Use 'fr' or 'us'.\n", argv[1]);
    }
}

static void cmd_sync(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    config_save_history();
    config_save();
    fs_sync();
}

static void cmd_exit(int argc, char* argv[]) {
    int status = EXIT_SUCCESS;
    if (argc >= 2) {
        int val = 0, neg = 0;
        const char *p = argv[1];
        if (*p == '-') { neg = 1; p++; }
        while (*p >= '0' && *p <= '9')
            val = val * 10 + (*p++ - '0');
        status = neg ? -val : val;
    }
    config_save_history();
    config_save();
    fs_sync();
    printf("End\n");
    exit(status);
}

static inline void outw(uint16_t port, uint16_t val) {
    asm volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static void cmd_shutdown(int argc, char* argv[]) {
    (void)argc; (void)argv;
    config_save_history();
    config_save();
    fs_sync();
    printf("Powering off...\n");
    asm volatile("cli");
    outw(0x604, 0x2000);   /* QEMU i440fx ACPI shutdown */
    outw(0xB004, 0x2000);  /* Bochs / older QEMU */
    /* If ACPI didn't work, fall back to halt */
    printf("ACPI shutdown failed. System halted.\n");
    while (1) asm volatile("hlt");
}

static void cmd_timedatectl(int argc, char* argv[]) {
    if (argc < 2 || strcmp(argv[1], "status") == 0) {
        /* Show status */
        datetime_t dt;
        config_get_datetime(&dt);
        system_config_t* cfg = config_get();
        
        /* Local time */
        printf("      Local time: %d-", dt.year);
        if (dt.month < 10) putchar('0'); printf("%d-", dt.month);
        if (dt.day < 10) putchar('0'); printf("%d ", dt.day);
        if (dt.hour < 10) putchar('0'); printf("%d:", dt.hour);
        if (dt.minute < 10) putchar('0'); printf("%d:", dt.minute);
        if (dt.second < 10) putchar('0'); printf("%d\n", dt.second);
        
        /* Universal time */
        printf("  Universal time: %d-", dt.year);
        if (dt.month < 10) putchar('0'); printf("%d-", dt.month);
        if (dt.day < 10) putchar('0'); printf("%d ", dt.day);
        if (dt.hour < 10) putchar('0'); printf("%d:", dt.hour);
        if (dt.minute < 10) putchar('0'); printf("%d:", dt.minute);
        if (dt.second < 10) putchar('0'); printf("%d\n", dt.second);
        
        printf("        Timezone: %s\n", config_get_timezone());
        printf("     Time format: %s\n", cfg->use_24h_format ? "24-hour" : "12-hour");
        
        uint32_t uptime = cfg->uptime_seconds;
        uint32_t hours = uptime / 3600;
        uint32_t minutes = (uptime % 3600) / 60;
        uint32_t seconds = uptime % 60;
        printf("          Uptime: %dh %dm %ds\n", (int)hours, (int)minutes, (int)seconds);
        
    } else if (strcmp(argv[1], "set-time") == 0) {
        if (argc < 3) {
            printf("Usage: timedatectl set-time HH:MM:SS\n");
            return;
        }
        
        /* Parse time HH:MM:SS */
        int hour = 0, minute = 0, second = 0;
        const char* p = argv[2];
        
        /* Parse hour */
        while (*p >= '0' && *p <= '9') hour = hour * 10 + (*p++ - '0');
        if (*p != ':') {
            printf("Invalid time format. Use HH:MM:SS\n");
            return;
        }
        p++;
        
        /* Parse minute */
        while (*p >= '0' && *p <= '9') minute = minute * 10 + (*p++ - '0');
        if (*p != ':') {
            printf("Invalid time format. Use HH:MM:SS\n");
            return;
        }
        p++;
        
        /* Parse second */
        while (*p >= '0' && *p <= '9') second = second * 10 + (*p++ - '0');
        
        if (hour > 23 || minute > 59 || second > 59) {
            printf("Invalid time values\n");
            return;
        }
        
        datetime_t dt;
        config_get_datetime(&dt);
        dt.hour = hour;
        dt.minute = minute;
        dt.second = second;
        config_set_datetime(&dt);
        printf("Time set to ");
        if (hour < 10) putchar('0'); printf("%d:", hour);
        if (minute < 10) putchar('0'); printf("%d:", minute);
        if (second < 10) putchar('0'); printf("%d\n", second);
        
    } else if (strcmp(argv[1], "set-date") == 0) {
        if (argc < 3) {
            printf("Usage: timedatectl set-date YYYY-MM-DD\n");
            return;
        }
        
        /* Parse date YYYY-MM-DD */
        int year = 0, month = 0, day = 0;
        const char* p = argv[2];
        
        /* Parse year */
        while (*p >= '0' && *p <= '9') year = year * 10 + (*p++ - '0');
        if (*p != '-') {
            printf("Invalid date format. Use YYYY-MM-DD\n");
            return;
        }
        p++;
        
        /* Parse month */
        while (*p >= '0' && *p <= '9') month = month * 10 + (*p++ - '0');
        if (*p != '-') {
            printf("Invalid date format. Use YYYY-MM-DD\n");
            return;
        }
        p++;
        
        /* Parse day */
        while (*p >= '0' && *p <= '9') day = day * 10 + (*p++ - '0');
        
        if (year < 1970 || year > 9999 || month < 1 || month > 12 || day < 1 || day > 31) {
            printf("Invalid date values\n");
            return;
        }
        
        datetime_t dt;
        config_get_datetime(&dt);
        dt.year = year;
        dt.month = month;
        dt.day = day;
        config_set_datetime(&dt);
        printf("Date set to %d-", year);
        if (month < 10) putchar('0'); printf("%d-", month);
        if (day < 10) putchar('0'); printf("%d\n", day);
        
    } else if (strcmp(argv[1], "set-timezone") == 0) {
        if (argc < 3) {
            printf("Usage: timedatectl set-timezone TIMEZONE\n");
            return;
        }
        
        config_set_timezone(argv[2]);
        printf("Timezone set to %s\n", argv[2]);
        
    } else if (strcmp(argv[1], "list-timezones") == 0) {
        printf("Available timezones:\n");
        printf("  UTC\n");
        printf("  Europe/Paris\n");
        printf("  Europe/London\n");
        printf("  Europe/Berlin\n");
        printf("  America/New_York\n");
        printf("  America/Los_Angeles\n");
        printf("  America/Chicago\n");
        printf("  Asia/Tokyo\n");
        printf("  Asia/Shanghai\n");
        printf("  Australia/Sydney\n");
        
    } else {
        printf("Unknown command '%s'\n", argv[1]);
        printf("Use 'man timedatectl' for help\n");
    }
}

static void cmd_ifconfig(int argc, char* argv[]) {
    net_config_t* config = net_get_config();
    
    if (argc == 1) {
        /* Display current configuration */
        printf("eth0: flags=");
        if (config->link_up) {
            printf("UP");
        } else {
            printf("DOWN");
        }
        printf("\n");
        
        printf("    inet ");
        net_print_ip(config->ip);
        printf("  netmask ");
        net_print_ip(config->netmask);
        printf("\n");
        
        printf("    ether ");
        net_print_mac(config->mac);
        printf("\n");
        
        printf("    gateway ");
        net_print_ip(config->gateway);
        printf("\n");
        
    } else if (argc >= 2) {
        const char* iface = argv[1];
        
        if (strcmp(iface, "eth0") != 0) {
            printf("Unknown interface: %s\n", iface);
            return;
        }
        
        if (argc == 3 && strcmp(argv[2], "up") == 0) {
            config->link_up = 1;
            printf("Interface eth0 enabled\n");
        } else if (argc == 3 && strcmp(argv[2], "down") == 0) {
            config->link_up = 0;
            printf("Interface eth0 disabled\n");
        } else if (argc == 4) {
            /* Set IP and netmask: ifconfig eth0 10.0.2.15 255.255.255.0 */
            /* Parse IP address */
            const char* ip_str = argv[2];
            int a = 0, b = 0, c = 0, d = 0;
            const char* p = ip_str;
            
            while (*p >= '0' && *p <= '9') a = a * 10 + (*p++ - '0');
            if (*p++ != '.') { printf("Invalid IP format\n"); return; }
            while (*p >= '0' && *p <= '9') b = b * 10 + (*p++ - '0');
            if (*p++ != '.') { printf("Invalid IP format\n"); return; }
            while (*p >= '0' && *p <= '9') c = c * 10 + (*p++ - '0');
            if (*p++ != '.') { printf("Invalid IP format\n"); return; }
            while (*p >= '0' && *p <= '9') d = d * 10 + (*p++ - '0');
            
            config->ip[0] = a;
            config->ip[1] = b;
            config->ip[2] = c;
            config->ip[3] = d;
            
            /* Parse netmask */
            const char* mask_str = argv[3];
            a = b = c = d = 0;
            p = mask_str;
            
            while (*p >= '0' && *p <= '9') a = a * 10 + (*p++ - '0');
            if (*p++ != '.') { printf("Invalid netmask format\n"); return; }
            while (*p >= '0' && *p <= '9') b = b * 10 + (*p++ - '0');
            if (*p++ != '.') { printf("Invalid netmask format\n"); return; }
            while (*p >= '0' && *p <= '9') c = c * 10 + (*p++ - '0');
            if (*p++ != '.') { printf("Invalid netmask format\n"); return; }
            while (*p >= '0' && *p <= '9') d = d * 10 + (*p++ - '0');
            
            config->netmask[0] = a;
            config->netmask[1] = b;
            config->netmask[2] = c;
            config->netmask[3] = d;
            
            printf("IP address set to ");
            net_print_ip(config->ip);
            printf("\n");
            printf("Netmask set to ");
            net_print_ip(config->netmask);
            printf("\n");
        } else {
            printf("Usage: ifconfig [interface] [up|down|IP NETMASK]\n");
        }
    }
}

static void cmd_ping(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: ping HOST\n");
        return;
    }
    
    const char* host = argv[1];
    
    /* Parse IP address */
    int a = 0, b = 0, c = 0, d = 0;
    const char* p = host;
    
    while (*p >= '0' && *p <= '9') a = a * 10 + (*p++ - '0');
    if (*p++ != '.') { printf("Invalid IP format\n"); return; }
    while (*p >= '0' && *p <= '9') b = b * 10 + (*p++ - '0');
    if (*p++ != '.') { printf("Invalid IP format\n"); return; }
    while (*p >= '0' && *p <= '9') c = c * 10 + (*p++ - '0');
    if (*p++ != '.') { printf("Invalid IP format\n"); return; }
    while (*p >= '0' && *p <= '9') d = d * 10 + (*p++ - '0');
    
    uint8_t dst_ip[4] = {a, b, c, d};
    
    printf("PING %d.%d.%d.%d\n", a, b, c, d);
    printf("Note: ICMP echo not supported by QEMU user networking\n");
    printf("Use 'arp' command to test network functionality\n");
    
    /* Send pings anyway for testing */
    for (int i = 1; i <= 4; i++) {
        icmp_send_echo_request(dst_ip, 1, i);
        
        /* Wait and process packets */
        for (int attempts = 0; attempts < 20; attempts++) {
            net_process_packets();
            for (volatile int j = 0; j < 500000; j++);
        }
        
        /* Delay between pings */
        for (volatile int j = 0; j < 1000000; j++);
    }
    
    printf("\n");
}

static void cmd_lspci(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    pci_scan_bus();
}

static void cmd_arp(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: arp IP\n");
        return;
    }
    
    const char* host = argv[1];
    
    /* Parse IP address */
    int a = 0, b = 0, c = 0, d = 0;
    const char* p = host;
    
    while (*p >= '0' && *p <= '9') a = a * 10 + (*p++ - '0');
    if (*p++ != '.') { printf("Invalid IP format\n"); return; }
    while (*p >= '0' && *p <= '9') b = b * 10 + (*p++ - '0');
    if (*p++ != '.') { printf("Invalid IP format\n"); return; }
    while (*p >= '0' && *p <= '9') c = c * 10 + (*p++ - '0');
    if (*p++ != '.') { printf("Invalid IP format\n"); return; }
    while (*p >= '0' && *p <= '9') d = d * 10 + (*p++ - '0');
    
    uint8_t target_ip[4] = {a, b, c, d};
    
    printf("ARP request for %d.%d.%d.%d ... ", a, b, c, d);
    
    /* Send ARP request */
    arp_send_request(target_ip);
    
    /* Wait and process packets */
    for (int i = 0; i < 20; i++) {
        net_process_packets();
        for (volatile int j = 0; j < 500000; j++);
    }
    
    printf("\n");
}

static void cmd_export(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: export VAR=value\n");
        return;
    }
    
    /* Parse VAR=value */
    char* equals = strchr(argv[1], '=');
    if (!equals) {
        printf("Invalid format. Use: export VAR=value\n");
        return;
    }
    
    /* Split into name and value */
    *equals = '\0';
    const char* name = argv[1];
    const char* value = equals + 1;
    
    if (env_set(name, value) == 0) {
        printf("%s=%s\n", name, value);
    } else {
        printf("Failed to set variable\n");
    }
}

static void cmd_env(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    env_list();
}

static void cmd_whoami(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    const char* user = env_get("USER");
    if (user) {
        printf("%s\n", user);
    } else {
        printf("unknown\n");
    }
}
