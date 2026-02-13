#include <kernel/shell.h>
#include <kernel/fs.h>
#include <kernel/tty.h>
#include <kernel/vi.h>
#include <kernel/config.h>
#include <kernel/net.h>
#include <kernel/pci.h>
#include <kernel/ip.h>
#include <kernel/dns.h>
#include <kernel/dhcp.h>
#include <kernel/httpd.h>
#include <kernel/quota.h>
#include <kernel/env.h>
#include <kernel/user.h>
#include <kernel/group.h>
#include <kernel/hostname.h>
#include <kernel/acpi.h>
#include <kernel/test.h>
#include <kernel/gfx.h>
#include <kernel/desktop.h>
#include <kernel/firewall.h>
#include <kernel/mouse.h>
#include <kernel/wm.h>
#include <kernel/idt.h>
#include <kernel/arp.h>
#include <kernel/task.h>
#include <kernel/sched.h>
#include <kernel/pipe.h>
#include <kernel/signal.h>
#include <kernel/shm.h>
#include <kernel/rtc.h>
#include <kernel/beep.h>
#include <kernel/pe_loader.h>
#include <kernel/elf_loader.h>
#include <kernel/tls.h>
#include <kernel/io.h>
#include <kernel/multiboot.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

int shell_exit_requested = 0;

/* ── Shell pipe infrastructure ── */
#define SHELL_PIPE_BUF_SIZE 4096
static char  shell_pipe_buf[SHELL_PIPE_BUF_SIZE];
static int   shell_pipe_len;
static int   shell_pipe_mode;  /* 1 = capturing output into pipe_buf */

/* Hook for printf redirection: called from putchar when pipe mode active */
void shell_pipe_putchar(char c) {
    if (shell_pipe_mode && shell_pipe_len < SHELL_PIPE_BUF_SIZE - 1)
        shell_pipe_buf[shell_pipe_len++] = c;
}

int shell_is_pipe_mode(void) {
    return shell_pipe_mode;
}

/* Foreground app (non-blocking command like top) */
static shell_fg_app_t *active_fg_app = NULL;

void shell_register_fg_app(shell_fg_app_t *app) { active_fg_app = app; }
void shell_unregister_fg_app(void) { active_fg_app = NULL; }
shell_fg_app_t *shell_get_fg_app(void) { return active_fg_app; }

#define MAX_ARGS 64

typedef void (*cmd_func_t)(int argc, char* argv[]);

#define CMD_FLAG_ROOT  (1 << 0)   /* requires root to run */

typedef struct {
    const char* name;
    cmd_func_t func;
    const char* short_desc;
    const char* help_text;
    const char* man_page;
    uint8_t flags;
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
static void cmd_chmod(int argc, char* argv[]);
static void cmd_chown(int argc, char* argv[]);
static void cmd_ln(int argc, char* argv[]);
static void cmd_readlink(int argc, char* argv[]);
static void cmd_su(int argc, char* argv[]);
static void cmd_id(int argc, char* argv[]);
static void cmd_useradd(int argc, char* argv[]);
static void cmd_userdel(int argc, char* argv[]);
static void cmd_test(int argc, char* argv[]);
static void cmd_logout(int argc, char* argv[]);
static void cmd_gfxdemo(int argc, char* argv[]);
static void cmd_nslookup(int argc, char* argv[]);
static void cmd_dhcp_cmd(int argc, char* argv[]);
static void cmd_httpd(int argc, char* argv[]);
static void cmd_quota(int argc, char* argv[]);
static void cmd_connect(int argc, char* argv[]);
static void cmd_firewall(int argc, char* argv[]);
static void cmd_top(int argc, char* argv[]);
static void cmd_kill(int argc, char* argv[]);
static void cmd_display(int argc, char* argv[]);
static void cmd_gfxbench(int argc, char* argv[]);
static void cmd_fps(int argc, char* argv[]);
static void cmd_spawn(int argc, char* argv[]);
static void cmd_shm(int argc, char* argv[]);
static void cmd_ntpdate(int argc, char* argv[]);
static void cmd_beep(int argc, char* argv[]);
static void cmd_winget(int argc, char* argv[]);
static void cmd_run(int argc, char* argv[]);
static void cmd_petest(int argc, char* argv[]);
static void cmd_petest_gui(int argc, char* argv[]);
static void cmd_sudo(int argc, char* argv[]);
static void cmd_threadtest(int argc, char* argv[]);
static void cmd_memtest(int argc, char* argv[]);
static void cmd_fstest(int argc, char* argv[]);
static void cmd_proctest(int argc, char* argv[]);
static void cmd_doom(int argc, char* argv[]);

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
        "    available shell commands with short descriptions.\n",
        0
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
        "    behavior and options.\n",
        0
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
        "    newline is written.\n",
        0
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
        "    exist or is a directory, an error message is printed.\n",
        0
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
        "    Flags may be combined: ls -la\n",
        0
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
        "    parent directory respectively.\n",
        0
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
        "    printed. The file is created with zero size.\n",
        0
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
        "    the cursor position to row 0, column 0.\n",
        0
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
        "    directory by walking the .. chain up to /.\n",
        0
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
        "    Use Up/Down in the shell to recall history.\n",
        0
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
        "    NAME already exists.\n",
        0
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
        "    ..). The root directory cannot be removed.\n",
        0
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
        "    :q!       Quit without saving\n",
        0
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
        "    Without arguments, prints the current layout.\n",
        0
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
        "    this command forces an immediate sync.\n",
        0
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
        "    0 indicates success, nonzero indicates failure.\n",
        0
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
        "    the CPU (same as 'exit').\n",
        CMD_FLAG_ROOT
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
        "        List common available timezones.\n",
        0
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
        "        Enable network interface\n",
        CMD_FLAG_ROOT
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
        "        Ping the default gateway\n",
        0
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
        "    device ID, and device class.\n",
        0
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
        "    This tests if network RX actually works.\n",
        0
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
        "    export HOME=/home/user\n",
        0
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
        "    variables and their values.\n",
        0
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
        "    Prints the name of the current user.\n",
        0
    },
    {
        "chmod", cmd_chmod,
        "Change file permissions",
        "chmod: chmod MODE FILE\n"
        "    Change file permissions. MODE is octal (e.g. 755).\n",
        "NAME\n"
        "    chmod - change file mode bits\n\n"
        "SYNOPSIS\n"
        "    chmod MODE FILE\n\n"
        "DESCRIPTION\n"
        "    Change the permissions of FILE to MODE.\n"
        "    MODE is an octal number (e.g. 755, 644).\n"
        "    Only the file owner or root can change permissions.\n",
        0
    },
    {
        "chown", cmd_chown,
        "Change file owner",
        "chown: chown USER[:GROUP] FILE\n"
        "    Change file owner and optionally group. Root only.\n",
        "NAME\n"
        "    chown - change file owner and group\n\n"
        "SYNOPSIS\n"
        "    chown USER[:GROUP] FILE\n\n"
        "DESCRIPTION\n"
        "    Change the owner (and optionally group) of FILE.\n"
        "    Only root can change file ownership.\n",
        CMD_FLAG_ROOT
    },
    {
        "ln", cmd_ln,
        "Create links between files",
        "ln: ln -s TARGET LINKNAME\n"
        "    Create a symbolic link to TARGET with name LINKNAME.\n",
        "NAME\n"
        "    ln - make links between files\n\n"
        "SYNOPSIS\n"
        "    ln -s TARGET LINKNAME\n\n"
        "DESCRIPTION\n"
        "    Create a symbolic link named LINKNAME pointing to TARGET.\n"
        "    The -s flag is required (only symlinks are supported).\n",
        0
    },
    {
        "readlink", cmd_readlink,
        "Display symlink target",
        "readlink: readlink LINK\n"
        "    Display the target of a symbolic link.\n",
        "NAME\n"
        "    readlink - print resolved symbolic links\n\n"
        "SYNOPSIS\n"
        "    readlink LINK\n\n"
        "DESCRIPTION\n"
        "    Print the target of the symbolic link LINK.\n",
        0
    },
    {
        "su", cmd_su,
        "Switch user",
        "su: su [USERNAME]\n"
        "    Switch to another user (default: root).\n",
        "NAME\n"
        "    su - switch user identity\n\n"
        "SYNOPSIS\n"
        "    su [USERNAME]\n\n"
        "DESCRIPTION\n"
        "    Switch to another user. Prompts for password unless\n"
        "    the current user is root. Default target is root.\n",
        0
    },
    {
        "sudo", cmd_sudo,
        "Execute a command as root",
        "sudo: sudo COMMAND [ARGS...]\n"
        "    Execute a command as root. Prompts for the current\n"
        "    user's password (not root's).\n",
        "NAME\n"
        "    sudo - execute a command as root\n\n"
        "SYNOPSIS\n"
        "    sudo COMMAND [ARGS...]\n\n"
        "DESCRIPTION\n"
        "    Run COMMAND with root privileges. Authenticates using\n"
        "    the current user's password. If already root, runs\n"
        "    the command without prompting.\n",
        0
    },
    {
        "id", cmd_id,
        "Display user identity",
        "id: id\n"
        "    Display current user and group IDs.\n",
        "NAME\n"
        "    id - print real and effective user and group IDs\n\n"
        "SYNOPSIS\n"
        "    id\n\n"
        "DESCRIPTION\n"
        "    Print user and group information for the current user.\n",
        0
    },
    {
        "useradd", cmd_useradd,
        "Create a new user",
        "useradd: useradd USERNAME\n"
        "    Create a new user account. Root only.\n",
        "NAME\n"
        "    useradd - create a new user\n\n"
        "SYNOPSIS\n"
        "    useradd USERNAME\n\n"
        "DESCRIPTION\n"
        "    Create a new user with auto-assigned UID, prompted\n"
        "    password, and home directory. Root only.\n",
        CMD_FLAG_ROOT
    },
    {
        "userdel", cmd_userdel,
        "Delete a user",
        "userdel: userdel [-r] USERNAME\n"
        "    Delete a user account. Root only.\n",
        "NAME\n"
        "    userdel - delete a user account\n\n"
        "SYNOPSIS\n"
        "    userdel [-r] USERNAME\n\n"
        "DESCRIPTION\n"
        "    Delete the user USERNAME. With -r, also remove\n"
        "    the user's home directory. Root only.\n",
        CMD_FLAG_ROOT
    },
    {
        "test", cmd_test,
        "Run regression tests",
        "test: test [crypto|tls]\n"
        "    Run all or specific test suites.\n",
        "NAME\n"
        "    test - run regression tests\n\n"
        "SYNOPSIS\n"
        "    test [SUITE]\n\n"
        "DESCRIPTION\n"
        "    Run all built-in test suites and print results.\n"
        "    Optional SUITE: crypto, tls\n",
        CMD_FLAG_ROOT
    },
    {
        "logout", cmd_logout,
        "Log out and return to login prompt",
        "logout: logout\n"
        "    Log out of the current session.\n",
        "NAME\n"
        "    logout - log out of the shell\n\n"
        "SYNOPSIS\n"
        "    logout\n\n"
        "DESCRIPTION\n"
        "    Saves state and returns to the login prompt.\n"
        "    The current user session is ended.\n",
        0
    },
    {
        "gfxdemo", cmd_gfxdemo,
        "Run a graphics framebuffer demo",
        "gfxdemo: gfxdemo\n"
        "    Draw shapes and colors using the VBE framebuffer.\n",
        "NAME\n"
        "    gfxdemo - graphics demo\n\n"
        "SYNOPSIS\n"
        "    gfxdemo\n\n"
        "DESCRIPTION\n"
        "    Demonstrates the framebuffer graphics API by drawing\n"
        "    rectangles, lines, and text. Press any key to exit.\n",
        CMD_FLAG_ROOT
    },
    {
        "nslookup", cmd_nslookup,
        "Query DNS to resolve a hostname",
        "nslookup: nslookup HOSTNAME\n"
        "    Resolve HOSTNAME to an IP address using DNS.\n",
        "NAME\n"
        "    nslookup - query Internet name servers\n\n"
        "SYNOPSIS\n"
        "    nslookup HOSTNAME\n\n"
        "DESCRIPTION\n"
        "    Sends a DNS type-A query to the configured DNS server\n"
        "    (default 10.0.2.3 for QEMU SLIRP) and prints the\n"
        "    resolved IPv4 address.\n",
        0
    },
    {
        "dhcp", cmd_dhcp_cmd,
        "Obtain an IP address via DHCP",
        "dhcp: dhcp\n"
        "    Run DHCP discovery to obtain an IP address.\n",
        "NAME\n"
        "    dhcp - Dynamic Host Configuration Protocol client\n\n"
        "SYNOPSIS\n"
        "    dhcp\n\n"
        "DESCRIPTION\n"
        "    Sends DHCP Discover/Offer/Request/Acknowledge sequence\n"
        "    to obtain a network configuration from the DHCP server.\n",
        CMD_FLAG_ROOT
    },
    {
        "httpd", cmd_httpd,
        "Start or stop the HTTP server",
        "httpd: httpd start|stop\n"
        "    Start or stop the built-in HTTP server on port 80.\n",
        "NAME\n"
        "    httpd - minimal HTTP/1.0 server\n\n"
        "SYNOPSIS\n"
        "    httpd start|stop\n\n"
        "DESCRIPTION\n"
        "    Starts a minimal HTTP server on port 80. It serves\n"
        "    static HTML for / and files from the filesystem.\n"
        "    Use 'httpd stop' to shut it down.\n",
        CMD_FLAG_ROOT
    },
    {
        "quota", cmd_quota,
        "View or set filesystem quotas",
        "quota: quota [-u USER] [-s USER INODES BLOCKS]\n"
        "    View or set per-user filesystem quotas.\n",
        "NAME\n"
        "    quota - manage filesystem quotas\n\n"
        "SYNOPSIS\n"
        "    quota [-u USER] [-s USER INODES BLOCKS]\n\n"
        "DESCRIPTION\n"
        "    Without arguments, shows quota for the current user.\n"
        "    -u USER   Show quota for USER (by UID).\n"
        "    -s USER INODES BLOCKS  Set quota limits for USER.\n"
        "    INODES and BLOCKS are maximum counts (0 = unlimited).\n",
        CMD_FLAG_ROOT
    },
    {
        "connect", cmd_connect,
        "Auto-configure network via DHCP",
        "connect: connect\n"
        "    Bring up the network by running DHCP discovery.\n"
        "    Displays assigned IP, netmask, and gateway on success.\n",
        "NAME\n"
        "    connect - auto-configure network via DHCP\n\n"
        "SYNOPSIS\n"
        "    connect\n\n"
        "DESCRIPTION\n"
        "    Checks that a NIC is present and the link is up,\n"
        "    then runs DHCP discovery to obtain an IP address,\n"
        "    netmask, and gateway from the network. Prints the\n"
        "    assigned configuration on success.\n",
        CMD_FLAG_ROOT
    },
    {
        "firewall", cmd_firewall,
        "Manage packet filtering rules",
        "firewall: firewall list|add|del|flush|default\n"
        "    Manage the packet filtering firewall.\n",
        "NAME\n"
        "    firewall - manage packet filtering rules\n\n"
        "SYNOPSIS\n"
        "    firewall list\n"
        "    firewall add allow|deny tcp|udp|icmp|all [SRC_IP[/MASK]] [PORT[-PORT]]\n"
        "    firewall del INDEX\n"
        "    firewall flush\n"
        "    firewall default allow|deny\n\n"
        "DESCRIPTION\n"
        "    A minimal stateless packet filter. Rules are evaluated\n"
        "    top-to-bottom; first match wins. Default policy applies\n"
        "    if no rule matches (default: allow).\n\n"
        "    list     Show all rules and default policy.\n"
        "    add      Add a rule. Protocol: tcp, udp, icmp, or all.\n"
        "             Optional SRC_IP with /MASK (e.g. 10.0.2.0/255.255.255.0).\n"
        "             Optional port or port range (e.g. 80 or 1024-65535).\n"
        "    del N    Delete rule at index N.\n"
        "    flush    Remove all rules.\n"
        "    default  Set default policy to allow or deny.\n",
        CMD_FLAG_ROOT
    },
    {
        "top", cmd_top,
        "Display live system information",
        "top: top\n"
        "    Display live-updating system stats including heap usage,\n"
        "    RAM, filesystem usage, and open windows.\n"
        "    Press 'q' to quit.\n",
        "NAME\n"
        "    top - display live system information\n\n"
        "SYNOPSIS\n"
        "    top\n\n"
        "DESCRIPTION\n"
        "    Shows a live-updating display of system stats: uptime,\n"
        "    heap memory usage, physical RAM, filesystem inode/block\n"
        "    usage, and a list of open windows. Refreshes every second.\n\n"
        "    Press 'q' to exit and return to the shell.\n",
        0
    },
    {
        "kill", cmd_kill,
        "Send a signal to a process",
        "kill: kill [-9|-INT|-TERM|-KILL|-USR1|-USR2|-PIPE] PID\n"
        "    Send a signal to the process with the given PID.\n",
        "NAME\n"
        "    kill - send a signal to a process\n\n"
        "SYNOPSIS\n"
        "    kill [-9|-INT|-TERM|-KILL|-USR1|-USR2|-PIPE] PID\n\n"
        "DESCRIPTION\n"
        "    Sends a signal to the process identified by PID.\n"
        "    Without a signal flag, sends SIGTERM (15). System\n"
        "    processes (idle, kernel, wm, shell) cannot be signaled.\n\n"
        "OPTIONS\n"
        "    -9, -KILL    Forcefully kill (uncatchable)\n"
        "    -INT         Send interrupt signal (2)\n"
        "    -TERM        Send termination signal (15, default)\n"
        "    -USR1        Send user-defined signal 1 (10)\n"
        "    -USR2        Send user-defined signal 2 (12)\n"
        "    -PIPE        Send broken pipe signal (13)\n",
        0
    },
    {
        "display", cmd_display,
        "Show real-time FPS and input monitor",
        "display: display\n"
        "    Show a live FPS counter, mouse coordinates, and input state.\n"
        "    Press 'q' to quit.\n",
        "NAME\n"
        "    display - real-time FPS and input monitor\n\n"
        "SYNOPSIS\n"
        "    display\n\n"
        "DESCRIPTION\n"
        "    Displays a fullscreen overlay with a live frames-per-second\n"
        "    counter, mouse position, button state, and a crosshair at\n"
        "    the current mouse coordinates. Useful for diagnosing input\n"
        "    and rendering issues. Press 'q' or ESC to exit.\n",
        CMD_FLAG_ROOT
    },
    {
        "gfxbench", cmd_gfxbench,
        "Run graphics rendering stress test",
        "gfxbench: gfxbench\n"
        "    Stress test the rendering pipeline at max throughput.\n"
        "    Press 'q' to quit early.\n",
        "NAME\n"
        "    gfxbench - graphics rendering stress test\n\n"
        "SYNOPSIS\n"
        "    gfxbench\n\n"
        "DESCRIPTION\n"
        "    Runs five stress phases with no frame cap: rect flood,\n"
        "    line storm, circle cascade, alpha blending, and combined\n"
        "    chaos. Each phase runs for 5 seconds. FPS is measured and\n"
        "    printed as a summary at the end. Press 'q' or ESC to quit.\n",
        CMD_FLAG_ROOT
    },
    {
        "fps", cmd_fps,
        "Toggle FPS overlay on screen",
        "fps: fps\n"
        "    Toggle a live FPS counter in the top-right corner of the desktop.\n",
        "NAME\n"
        "    fps - toggle FPS overlay\n\n"
        "SYNOPSIS\n"
        "    fps\n\n"
        "DESCRIPTION\n"
        "    Toggles a persistent FPS counter overlay on the top-right\n"
        "    corner of the desktop. The counter updates every second and\n"
        "    shows the number of WM composites per second. Run 'fps'\n"
        "    again to turn it off.\n",
        CMD_FLAG_ROOT
    },
    {
        "spawn", cmd_spawn,
        "Spawn a background thread",
        "spawn: spawn [counter|hog|user-counter]\n"
        "    Spawn a background thread for testing preemptive multitasking.\n"
        "    counter      — prints a number every second (ring 0)\n"
        "    hog          — CPU-intensive loop (watchdog will kill it)\n"
        "    user-counter — prints a number every second (ring 3)\n",
        "NAME\n"
        "    spawn - spawn a background thread\n\n"
        "SYNOPSIS\n"
        "    spawn [counter|hog|user-counter]\n\n"
        "DESCRIPTION\n"
        "    Creates a new thread running in the background.\n"
        "    The thread runs preemptively alongside the shell.\n"
        "    Use 'kill PID' to terminate a spawned thread.\n"
        "    Types:\n"
        "      counter      - increments and prints a counter every second (ring 0)\n"
        "      hog          - infinite CPU loop (watchdog kills after 5s)\n"
        "      user-counter - like counter but runs in ring 3 (user mode)\n",
        CMD_FLAG_ROOT
    },
    {
        "shm", cmd_shm,
        "Manage shared memory regions",
        "shm: shm [list|create NAME SIZE]\n"
        "    Manage shared memory regions for inter-process communication.\n",
        "NAME\n"
        "    shm - manage shared memory regions\n\n"
        "SYNOPSIS\n"
        "    shm list\n"
        "    shm create NAME SIZE\n\n"
        "DESCRIPTION\n"
        "    Manages named shared memory regions. Regions can be\n"
        "    created from the shell and attached by user-mode tasks\n"
        "    via the SYS_SHM_ATTACH syscall.\n\n"
        "    list               Show all active shared memory regions.\n"
        "    create NAME SIZE   Create a region with given name and size in bytes.\n",
        CMD_FLAG_ROOT
    },
    {
        "ntpdate", cmd_ntpdate,
        "Synchronize system clock via NTP",
        "ntpdate: ntpdate\n"
        "    Sync system clock from pool.ntp.org via NTP.\n",
        "NAME\n"
        "    ntpdate - set date and time via NTP\n\n"
        "SYNOPSIS\n"
        "    ntpdate\n\n"
        "DESCRIPTION\n"
        "    Contacts pool.ntp.org via UDP port 123 to obtain\n"
        "    the current time and updates the system clock.\n"
        "    Requires an active network connection.\n",
        CMD_FLAG_ROOT
    },
    {
        "beep", cmd_beep,
        "Play a tone on the PC speaker",
        "beep: beep [FREQ MS | startup | error | ok | notify]\n"
        "    Play a tone on the PC speaker.\n",
        "NAME\n"
        "    beep - PC speaker tone generator\n\n"
        "SYNOPSIS\n"
        "    beep [FREQ DURATION_MS]\n"
        "    beep startup|error|ok|notify\n\n"
        "DESCRIPTION\n"
        "    Plays a tone using PIT channel 2 and the PC speaker.\n"
        "    With no arguments, plays a default 880Hz beep.\n",
        CMD_FLAG_ROOT
    },
    {
        "run", cmd_run,
        "Run a Windows .exe file",
        "run: run FILE.exe\n"
        "    Load and execute a PE32 Windows executable.\n",
        "NAME\n"
        "    run - execute a Windows PE32 executable\n\n"
        "SYNOPSIS\n"
        "    run FILE.exe\n\n"
        "DESCRIPTION\n"
        "    Loads a PE32 (.exe) file using the Win32 compatibility\n"
        "    layer. The executable runs natively with Win32 API calls\n"
        "    translated to ImposOS equivalents.\n",
        0
    },
    {
        "winget", cmd_winget,
        "Windows package manager",
        "winget: winget [install|list|search] [PACKAGE]\n"
        "    Manage Windows application packages.\n",
        "NAME\n"
        "    winget - Windows package manager for ImposOS\n\n"
        "SYNOPSIS\n"
        "    winget install PACKAGE\n"
        "    winget list\n"
        "    winget search QUERY\n\n"
        "DESCRIPTION\n"
        "    Download, install, and manage Windows applications.\n"
        "    Packages are PE32 executables fetched from the network\n"
        "    and stored in the local filesystem.\n",
        0
    },
    {
        "petest", cmd_petest,
        "Create and run a test Win32 .exe",
        "petest: petest\n"
        "    Write an embedded hello.exe to disk and run it.\n",
        "NAME\n"
        "    petest - test the PE32 loader with a built-in .exe\n\n"
        "SYNOPSIS\n"
        "    petest\n\n"
        "DESCRIPTION\n"
        "    Creates hello.exe on the filesystem and executes it\n"
        "    via the PE loader. The .exe imports puts() from\n"
        "    msvcrt.dll and ExitProcess() from kernel32.dll.\n",
        CMD_FLAG_ROOT
    },
    {
        "petest-gui", cmd_petest_gui,
        "Run a Win32 GUI test application",
        "petest-gui: petest-gui\n"
        "    Write an embedded Win32 GUI .exe to disk and run it.\n",
        "NAME\n"
        "    petest-gui - test the PE32 loader with a GUI .exe\n\n"
        "SYNOPSIS\n"
        "    petest-gui\n\n"
        "DESCRIPTION\n"
        "    Creates hello_gui.exe on the filesystem and executes it\n"
        "    via the PE loader. Opens a window with text and colored\n"
        "    rectangles using Win32 user32/gdi32 API shims.\n",
        CMD_FLAG_ROOT
    },
    {
        "threadtest", cmd_threadtest,
        "Run Win32 threading tests",
        "threadtest: threadtest\n"
        "    Write thread_test.exe to disk and run it.\n",
        "NAME\n"
        "    threadtest - test Win32 threading primitives\n\n"
        "SYNOPSIS\n"
        "    threadtest\n\n"
        "DESCRIPTION\n"
        "    Tests CreateThread, CriticalSection, Events,\n"
        "    and Interlocked operations via a Win32 PE .exe.\n",
        CMD_FLAG_ROOT
    },
    {
        "memtest", cmd_memtest,
        "Run Win32 memory management tests",
        "memtest: memtest\n"
        "    Write mem_test.exe to disk and run it.\n",
        "NAME\n"
        "    memtest - test Win32 memory APIs\n\n"
        "SYNOPSIS\n"
        "    memtest\n\n"
        "DESCRIPTION\n"
        "    Tests VirtualAlloc, VirtualProtect, VirtualQuery,\n"
        "    VirtualFree, and GlobalAlloc via a Win32 PE .exe.\n",
        CMD_FLAG_ROOT
    },
    {
        "fstest", cmd_fstest,
        "Run Win32 file system tests",
        "fstest: fstest\n"
        "    Write fs_test.exe to disk and run it.\n",
        "NAME\n"
        "    fstest - test Win32 file system APIs\n\n"
        "SYNOPSIS\n"
        "    fstest\n\n"
        "DESCRIPTION\n"
        "    Tests CreateFile, ReadFile, WriteFile, SetFilePointer,\n"
        "    FindFirstFile, CopyFile, DeleteFile, and path queries.\n",
        CMD_FLAG_ROOT
    },
    {
        "proctest", cmd_proctest,
        "Run Win32 process creation tests",
        "proctest: proctest\n"
        "    Write proc_test.exe to disk and run it.\n",
        "NAME\n"
        "    proctest - test Win32 process APIs\n\n"
        "SYNOPSIS\n"
        "    proctest\n\n"
        "DESCRIPTION\n"
        "    Tests CreateProcessA, WaitForSingleObject on process,\n"
        "    GetExitCodeProcess, CreatePipe, and DuplicateHandle.\n",
        CMD_FLAG_ROOT
    },
    {
        "doom", cmd_doom,
        "Play DOOM (requires doom1.wad module)",
        "doom: doom\n"
        "    Launch DOOM. Requires doom1.wad loaded as GRUB module.\n"
        "    Controls: arrows=move, Ctrl=fire, Space=use, Shift=run\n"
        "    ESC=menu, 1-7=weapons, Tab=map, F1=help\n",
        "NAME\n"
        "    doom - play DOOM\n\n"
        "SYNOPSIS\n"
        "    doom\n\n"
        "DESCRIPTION\n"
        "    Launches the DOOM engine using the doom1.wad file loaded\n"
        "    as a GRUB multiboot module. The game renders at 320x200\n"
        "    scaled to fill the screen. Press ESC for the menu and\n"
        "    select Quit Game to return to the shell.\n",
        0
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

void shell_initialize_subsystems(void) {
    fs_initialize();

    /* Mount initrd if available (after fs_initialize, before config) */
    if (initrd_data && initrd_size > 0) {
        fs_mount_initrd(initrd_data, initrd_size);
    }

    config_initialize();
    rtc_init();
    net_initialize();
    env_initialize();
    hostname_initialize();
    user_initialize();
    group_initialize();
    quota_initialize();
}

int shell_needs_setup(void) {
    return !user_system_initialized();
}

void shell_initialize(void) {
    shell_initialize_subsystems();

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
            for (size_t i = 0; i < NUM_COMMANDS && completion_matches_count < 32; i++) {
                if ((commands[i].flags & CMD_FLAG_ROOT) && !is_root)
                    continue;
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

/* ── Pipe right-side commands ── */

static void pipe_cmd_grep(const char *buf, int len, const char *pattern) {
    if (!pattern) {
        printf("grep: missing pattern\n");
        return;
    }
    const char *p = buf;
    const char *end = buf + len;
    while (p < end) {
        const char *nl = p;
        while (nl < end && *nl != '\n') nl++;
        int line_len = nl - p;
        /* Check if line contains pattern */
        for (int i = 0; i <= line_len - (int)strlen(pattern); i++) {
            if (memcmp(p + i, pattern, strlen(pattern)) == 0) {
                /* Print matching line */
                for (int j = 0; j < line_len; j++)
                    putchar(p[j]);
                putchar('\n');
                break;
            }
        }
        p = (nl < end) ? nl + 1 : nl;
    }
}

static void pipe_cmd_cat(const char *buf, int len) {
    for (int i = 0; i < len; i++)
        putchar(buf[i]);
}

static void pipe_cmd_wc(const char *buf, int len) {
    int lines = 0, words = 0, chars = len;
    int in_word = 0;
    for (int i = 0; i < len; i++) {
        if (buf[i] == '\n') lines++;
        if (buf[i] == ' ' || buf[i] == '\n' || buf[i] == '\t') {
            in_word = 0;
        } else {
            if (!in_word) words++;
            in_word = 1;
        }
    }
    /* Count final line if not newline-terminated */
    if (len > 0 && buf[len - 1] != '\n') lines++;
    printf("  %d  %d  %d\n", lines, words, chars);
}

/* ═══ DOOM ═════════════════════════════════════════════════════════ */

extern void doomgeneric_Create(int argc, char **argv);
extern void doomgeneric_Tick(void);
extern uint8_t *doom_wad_data;
extern uint32_t doom_wad_size;

/* Doom calls exit() — we use setjmp/longjmp to return to shell */
#include <setjmp.h>
static jmp_buf doom_exit_jmp;
static int doom_running = 0;

void doom_exit_to_shell(void) {
    if (doom_running)
        longjmp(doom_exit_jmp, 1);
}

static void cmd_doom(int argc, char* argv[]) {
    (void)argc; (void)argv;

    if (!gfx_is_active()) {
        printf("doom: requires graphical mode\n");
        return;
    }
    if (!doom_wad_data || doom_wad_size == 0) {
        printf("doom: no WAD file loaded (add doom1.wad as GRUB module)\n");
        return;
    }

    printf("Starting DOOM...\n");

    /* Disable WM idle callback while doom runs */
    keyboard_set_idle_callback(0);

    /* Redirect exit() to doom's longjmp so any exit() call in doom
       returns here instead of freezing the system */
    exit_set_restart_point(&doom_exit_jmp);

    doom_running = 1;
    if (setjmp(doom_exit_jmp) == 0) {
        char *doom_argv[] = { "doom", "-iwad", "doom1.wad", NULL };
        doomgeneric_Create(3, doom_argv);

        while (doom_running) {
            doomgeneric_Tick();
        }
    }
    doom_running = 0;

    /* Clear exit() redirect so it doesn't jump back into stale doom state */
    exit_set_restart_point(0);

    /* Restore idle callback and redraw desktop */
    keyboard_set_idle_callback(desktop_get_idle_terminal_cb());
    terminal_clear();
    if (gfx_is_active())
        wm_composite();
}

void shell_process_command(char* command) {
    /* Check for pipe operator */
    char *pipe_pos = strchr(command, '|');
    if (pipe_pos) {
        /* Split into left and right commands */
        *pipe_pos = '\0';
        char *left_cmd = command;
        char *right_cmd = pipe_pos + 1;

        /* Trim leading spaces on right command */
        while (*right_cmd == ' ') right_cmd++;

        /* Trim trailing spaces on left command */
        char *lend = pipe_pos - 1;
        while (lend > left_cmd && *lend == ' ') { *lend = '\0'; lend--; }

        /* Run left command with output captured */
        shell_pipe_mode = 1;
        shell_pipe_len = 0;
        shell_pipe_buf[0] = '\0';

        /* Parse and execute left command */
        char *argv[MAX_ARGS];
        int argc = 0;
        char *token = strtok(left_cmd, " ");
        while (token != NULL && argc < MAX_ARGS) {
            argv[argc++] = token;
            token = strtok(NULL, " ");
        }

        if (argc > 0) {
            for (size_t i = 0; i < NUM_COMMANDS; i++) {
                if (strcmp(argv[0], commands[i].name) == 0) {
                    if ((commands[i].flags & CMD_FLAG_ROOT) && user_get_current_uid() != 0) {
                        printf("%s: permission denied (requires root)\n", argv[0]);
                    } else {
                        commands[i].func(argc, argv);
                    }
                    break;
                }
            }
        }

        shell_pipe_mode = 0;
        shell_pipe_buf[shell_pipe_len] = '\0';

        /* Parse right command */
        char *rargv[MAX_ARGS];
        int rargc = 0;
        token = strtok(right_cmd, " ");
        while (token != NULL && rargc < MAX_ARGS) {
            rargv[rargc++] = token;
            token = strtok(NULL, " ");
        }

        if (rargc > 0) {
            if (strcmp(rargv[0], "grep") == 0)
                pipe_cmd_grep(shell_pipe_buf, shell_pipe_len, rargc > 1 ? rargv[1] : NULL);
            else if (strcmp(rargv[0], "cat") == 0)
                pipe_cmd_cat(shell_pipe_buf, shell_pipe_len);
            else if (strcmp(rargv[0], "wc") == 0)
                pipe_cmd_wc(shell_pipe_buf, shell_pipe_len);
            else
                printf("%s: pipe command not supported\n", rargv[0]);
        }
        return;
    }

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
            if ((commands[i].flags & CMD_FLAG_ROOT) && user_get_current_uid() != 0) {
                printf("%s: permission denied (requires root)\n", argv[0]);
                return;
            }
            commands[i].func(argc, argv);
            return;
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
            return;
        }

        char path_buf[64];
        snprintf(path_buf, sizeof(path_buf), "/bin/%s", name);
        ret = elf_run_argv(path_buf, argc, (const char **)argv);
        if (ret >= 0) {
            task_info_t *t = task_get(ret);
            while (t && t->active && t->state != TASK_STATE_ZOMBIE)
                task_yield();
            return;
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

    int is_root = (user_get_current_uid() == 0);
    printf("Available commands:\n");
    for (size_t i = 0; i < NUM_COMMANDS; i++) {
        if ((commands[i].flags & CMD_FLAG_ROOT) && !is_root)
            continue;
        if (commands[i].flags & CMD_FLAG_ROOT)
            printf("  %s [root] - %s\n", commands[i].name, commands[i].short_desc);
        else
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

    uint8_t* buffer = (uint8_t*)malloc(MAX_FILE_SIZE);
    if (!buffer) {
        printf("cat: out of memory\n");
        return;
    }
    size_t size = MAX_FILE_SIZE;
    if (fs_read_file(argv[1], buffer, &size) == 0) {
        for (size_t i = 0; i < size; i++) {
            putchar(buffer[i]);
        }
        printf("\n");
    } else {
        printf("cat: %s: No such file\n", argv[1]);
    }
    free(buffer);
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
    if (gfx_is_active())
        desktop_draw_chrome();
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
    (void)argc; (void)argv;
    config_save_history();
    config_save();
    fs_sync();
    if (gfx_is_active()) {
        shell_exit_requested = 1;
        return;
    }
    exit(0);
}

static void cmd_shutdown(int argc, char* argv[]) {
    (void)argc; (void)argv;
    config_save_history();
    config_save();
    fs_sync();
    printf("Powering off...\n");
    acpi_shutdown();
}

static void cmd_timedatectl(int argc, char* argv[]) {
    if (argc < 2 || strcmp(argv[1], "status") == 0) {
        /* Show status */
        datetime_t dt;
        config_get_datetime(&dt);
        system_config_t* cfg = config_get();
        
        /* Local time */
        printf("      Local time: %d-", dt.year);
        if (dt.month < 10) { putchar('0'); } printf("%d-", dt.month);
        if (dt.day < 10) { putchar('0'); } printf("%d ", dt.day);
        if (dt.hour < 10) { putchar('0'); } printf("%d:", dt.hour);
        if (dt.minute < 10) { putchar('0'); } printf("%d:", dt.minute);
        if (dt.second < 10) { putchar('0'); } printf("%d\n", dt.second);

        /* Universal time */
        printf("  Universal time: %d-", dt.year);
        if (dt.month < 10) { putchar('0'); } printf("%d-", dt.month);
        if (dt.day < 10) { putchar('0'); } printf("%d ", dt.day);
        if (dt.hour < 10) { putchar('0'); } printf("%d:", dt.hour);
        if (dt.minute < 10) { putchar('0'); } printf("%d:", dt.minute);
        if (dt.second < 10) { putchar('0'); } printf("%d\n", dt.second);
        
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
        if (hour < 10) { putchar('0'); } printf("%d:", hour);
        if (minute < 10) { putchar('0'); } printf("%d:", minute);
        if (second < 10) { putchar('0'); } printf("%d\n", second);
        
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
        if (month < 10) { putchar('0'); } printf("%d-", month);
        if (day < 10) { putchar('0'); } printf("%d\n", day);
        
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

    int ping_tid = task_register("ping", 1, -1);
    for (int i = 1; i <= 4; i++) {
        if (ping_tid >= 0 && task_check_killed(ping_tid)) break;
        icmp_send_echo_request(dst_ip, 1, i);

        /* Wait and process packets */
        for (int attempts = 0; attempts < 20; attempts++) {
            net_process_packets();
            for (volatile int j = 0; j < 500000; j++);
        }

        /* Delay between pings */
        for (volatile int j = 0; j < 1000000; j++);
    }
    if (ping_tid >= 0) task_unregister(ping_tid);

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

static void cmd_chmod(int argc, char* argv[]) {
    if (argc < 3) {
        printf("Usage: chmod MODE FILE\n");
        return;
    }

    /* Parse octal mode */
    const char* s = argv[1];
    uint16_t mode = 0;
    while (*s >= '0' && *s <= '7') {
        mode = mode * 8 + (*s - '0');
        s++;
    }
    if (*s != '\0' || mode > 0777) {
        printf("chmod: invalid mode '%s'\n", argv[1]);
        return;
    }

    if (fs_chmod(argv[2], mode) != 0) {
        printf("chmod: cannot change permissions of '%s'\n", argv[2]);
    }
}

static void cmd_chown(int argc, char* argv[]) {
    if (argc < 3) {
        printf("Usage: chown USER[:GROUP] FILE\n");
        return;
    }

    /* Parse user[:group] */
    char user_part[32] = {0};
    char group_part[32] = {0};
    const char* p = argv[1];
    size_t i = 0;

    while (*p && *p != ':' && i < sizeof(user_part) - 1) {
        user_part[i++] = *p++;
    }
    user_part[i] = '\0';

    if (*p == ':') {
        p++;
        i = 0;
        while (*p && i < sizeof(group_part) - 1) {
            group_part[i++] = *p++;
        }
        group_part[i] = '\0';
    }

    user_t* u = user_get(user_part);
    if (!u) {
        printf("chown: invalid user '%s'\n", user_part);
        return;
    }

    uint16_t gid = u->gid;
    if (group_part[0]) {
        group_t* g = group_get_by_name(group_part);
        if (!g) {
            printf("chown: invalid group '%s'\n", group_part);
            return;
        }
        gid = g->gid;
    }

    if (fs_chown(argv[2], u->uid, gid) != 0) {
        printf("chown: cannot change owner of '%s'\n", argv[2]);
    }
}

static void cmd_ln(int argc, char* argv[]) {
    if (argc < 4 || strcmp(argv[1], "-s") != 0) {
        printf("Usage: ln -s TARGET LINKNAME\n");
        return;
    }

    if (fs_create_symlink(argv[2], argv[3]) != 0) {
        printf("ln: cannot create symbolic link '%s'\n", argv[3]);
    }
}

static void cmd_readlink(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: readlink LINK\n");
        return;
    }

    char buf[512];
    if (fs_readlink(argv[1], buf, sizeof(buf)) == 0) {
        printf("%s\n", buf);
    } else {
        printf("readlink: '%s': not a symlink\n", argv[1]);
    }
}

static void cmd_su(int argc, char* argv[]) {
    const char* target = "root";
    if (argc >= 2) {
        target = argv[1];
    }

    user_t* u = user_get(target);
    if (!u) {
        printf("su: user '%s' does not exist\n", target);
        return;
    }

    /* Root doesn't need password */
    if (user_get_current_uid() != 0) {
        printf("Password: ");
        char password[64];
        size_t len = 0;

        while (len < sizeof(password) - 1) {
            int c = getchar();
            if (c == '\n' || c == '\r') break;
            if (c == '\b' || c == 127) {
                if (len > 0) {
                    len--;
                    printf("\b \b");
                }
            } else if (c >= 32 && c < 127) {
                password[len++] = c;
                putchar('*');
            }
        }
        password[len] = '\0';
        printf("\n");

        if (!user_authenticate(target, password)) {
            printf("su: Authentication failure\n");
            return;
        }
    }

    user_set_current(u->username);
    fs_change_directory(u->home);
}

static void cmd_sudo(int argc, char* argv[]) {
    if (argc < 2) {
        printf("usage: sudo <command> [args...]\n");
        return;
    }

    const char* current_user = user_get_current();
    if (!current_user) {
        printf("sudo: no current user\n");
        return;
    }

    /* Root doesn't need password */
    if (user_get_current_uid() != 0) {
        printf("[sudo] password for %s: ", current_user);
        char password[64];
        size_t len = 0;

        while (len < sizeof(password) - 1) {
            int c = getchar();
            if (c == '\n' || c == '\r') break;
            if (c == '\b' || c == 127) {
                if (len > 0) {
                    len--;
                    printf("\b \b");
                }
            } else if (c >= 32 && c < 127) {
                password[len++] = c;
                putchar('*');
            }
        }
        password[len] = '\0';
        printf("\n");

        if (!user_authenticate(current_user, password)) {
            printf("sudo: Authentication failure\n");
            return;
        }
    }

    /* Reconstruct the command string from argv[1..] */
    char cmd_buf[256];
    size_t pos = 0;
    for (int i = 1; i < argc && pos < sizeof(cmd_buf) - 1; i++) {
        if (i > 1 && pos < sizeof(cmd_buf) - 1)
            cmd_buf[pos++] = ' ';
        size_t alen = strlen(argv[i]);
        if (pos + alen >= sizeof(cmd_buf) - 1)
            alen = sizeof(cmd_buf) - 1 - pos;
        memcpy(cmd_buf + pos, argv[i], alen);
        pos += alen;
    }
    cmd_buf[pos] = '\0';

    /* Switch to root, run command, restore user */
    user_set_current("root");
    shell_process_command(cmd_buf);
    user_set_current(current_user);
}

static void cmd_id(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    const char* name = user_get_current();
    if (!name) {
        printf("id: no current user\n");
        return;
    }

    user_t* u = user_get(name);
    if (!u) {
        printf("id: cannot find user\n");
        return;
    }

    printf("uid=%d(%s) gid=%d", u->uid, u->username, u->gid);

    /* Show group name */
    group_t* g = group_get_by_gid(u->gid);
    if (g) {
        printf("(%s)", g->name);
    }

    /* Show all groups */
    printf(" groups=%d", u->gid);
    if (g) printf("(%s)", g->name);

    for (int i = 0; i < MAX_GROUPS; i++) {
        group_t* grp = group_get_by_index(i);
        if (grp && grp->gid != u->gid && group_is_member(grp->gid, u->username)) {
            printf(",%d(%s)", grp->gid, grp->name);
        }
    }

    printf("\n");
}

static void cmd_useradd(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: useradd USERNAME\n");
        return;
    }

    if (user_get_current_uid() != 0) {
        printf("useradd: only root can create users\n");
        return;
    }

    const char* username = argv[1];

    if (user_exists(username)) {
        printf("useradd: user '%s' already exists\n", username);
        return;
    }

    /* Prompt for password */
    printf("Password for %s: ", username);
    char password[64];
    size_t len = 0;

    while (len < sizeof(password) - 1) {
        int c = getchar();
        if (c == '\n' || c == '\r') break;
        if (c == '\b' || c == 127) {
            if (len > 0) {
                len--;
                printf("\b \b");
            }
        } else if (c >= 32 && c < 127) {
            password[len++] = c;
            putchar('*');
        }
    }
    password[len] = '\0';
    printf("\n");

    uint16_t uid = user_next_uid();
    uint16_t gid = uid;  /* Private group */

    char home[128];
    snprintf(home, sizeof(home), "/home/%s", username);

    /* Create home directory */
    fs_create_file("/home", 1);  /* Ensure /home exists */
    fs_create_file(home, 1);

    /* Create private group */
    group_create(username, gid);
    group_add_member(gid, username);

    /* Create user */
    if (user_create(username, password, home, uid, gid) != 0) {
        printf("useradd: failed to create user\n");
        return;
    }

    /* Create standard home subdirectories */
    user_create_home_dirs(home);

    /* Set home directory ownership */
    fs_chown(home, uid, gid);

    user_save();
    group_save();
    printf("User '%s' created (uid=%d, gid=%d)\n", username, uid, gid);
}

static void cmd_userdel(int argc, char* argv[]) {
    if (user_get_current_uid() != 0) {
        printf("userdel: only root can delete users\n");
        return;
    }

    int remove_home = 0;
    const char* username = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-r") == 0) {
            remove_home = 1;
        } else {
            username = argv[i];
        }
    }

    if (!username) {
        printf("Usage: userdel [-r] USERNAME\n");
        return;
    }

    user_t* u = user_get(username);
    if (!u) {
        printf("userdel: user '%s' does not exist\n", username);
        return;
    }

    if (strcmp(username, "root") == 0) {
        printf("userdel: cannot delete root\n");
        return;
    }

    char home[128];
    strncpy(home, u->home, sizeof(home) - 1);
    home[sizeof(home) - 1] = '\0';

    user_delete(username);
    user_save();

    if (remove_home) {
        fs_delete_file(home);
    }

    printf("User '%s' deleted\n", username);
}

/* Forward declarations for individual test suites */
extern void test_crypto(void);
extern void test_tls(void);

static void cmd_test(int argc, char* argv[]) {
    if (argc >= 2) {
        if (strcmp(argv[1], "crypto") == 0) {
            test_crypto();
            return;
        }
        if (strcmp(argv[1], "tls") == 0) {
            test_tls();
            return;
        }
        printf("Unknown test suite: %s\n", argv[1]);
        printf("Available: crypto, tls (or no args for all)\n");
        return;
    }
    test_run_all();
}

static void cmd_logout(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    config_save_history();
    config_save();
    fs_sync();
    if (gfx_is_active()) {
        shell_exit_requested = 1;
        return;
    }
    printf("Logging out...\n");
    exit(0);
}

/* ═══ Integer sine/cosine table (0..63 → 0..255, quarter-wave) ══ */
static const int16_t sin_tab64[65] = {
      0,  6, 13, 19, 25, 31, 37, 44, 50, 56, 62, 68, 74, 80, 86, 92,
     97,103,109,114,120,125,130,136,141,146,150,155,160,164,169,173,
    177,181,185,189,193,196,200,203,206,209,212,215,218,220,223,225,
    227,229,231,233,234,236,237,238,240,241,241,242,243,243,243,244,244
};

/* Returns sin(angle)*256 where angle is 0..1023 for full circle */
static int isin(int angle) {
    angle = angle & 1023;
    int q = angle >> 8;          /* quadrant 0-3 */
    int idx = angle & 255;       /* position in quadrant (0-255) */
    /* Map 0-255 to 0-64 table index */
    int ti = idx >> 2;           /* 0..63 */
    int val = sin_tab64[ti];
    switch (q) {
        case 0: return  val;
        case 1: return  sin_tab64[64 - ti];
        case 2: return -val;
        case 3: return -sin_tab64[64 - ti];
    }
    return 0;
}
static int icos(int angle) { return isin(angle + 256); }

/* Interpolate between two colors by t (0..255) */
static uint32_t color_lerp(uint32_t a, uint32_t b, int t) {
    int ra = (a >> 16) & 0xFF, ga = (a >> 8) & 0xFF, ba = a & 0xFF;
    int rb = (b >> 16) & 0xFF, gb = (b >> 8) & 0xFF, bb = b & 0xFF;
    int r = ra + (rb - ra) * t / 255;
    int g = ga + (gb - ga) * t / 255;
    int bl = ba + (bb - ba) * t / 255;
    return GFX_RGB(r, g, bl);
}

/* HSV to RGB: h 0..1023, s,v 0..255 */
static uint32_t hsv_to_rgb(int h, int s, int v) {
    h = h & 1023;
    int region = h * 6 / 1024;
    int remainder = (h * 6 - region * 1024) * 255 / 1024;
    int p = v * (255 - s) / 255;
    int q = v * (255 - (s * remainder / 255)) / 255;
    int t2 = v * (255 - (s * (255 - remainder) / 255)) / 255;
    switch (region) {
        case 0:  return GFX_RGB(v, t2, p);
        case 1:  return GFX_RGB(q, v, p);
        case 2:  return GFX_RGB(p, v, t2);
        case 3:  return GFX_RGB(p, q, v);
        case 4:  return GFX_RGB(t2, p, v);
        default: return GFX_RGB(v, p, q);
    }
}

/* Draw animated background gradient */
static void demo_draw_bg(int W, int H, int frame) {
    /* Shifting dark gradient */
    int phase = frame * 2;
    for (int y = 0; y < H; y++) {
        int t = y * 255 / H;
        int r = 8 + (isin(phase + y) * 8 / 256);
        int g = 10 + (isin(phase + y + 200) * 6 / 256);
        int b = 25 + t * 20 / 255 + (isin(phase + y + 400) * 5 / 256);
        if (r < 0) r = 0;
        if (g < 0) g = 0;
        if (b < 0) b = 0;
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        gfx_fill_rect(0, y, W, 1, GFX_RGB(r, g, b));
    }
}

/* Scene 1: Orbiting circles with trails */
static void demo_scene_orbits(int W, int H, int frame) {
    demo_draw_bg(W, H, frame);

    int cx = W / 2, cy = H / 2 - 30;

    /* Title */
    gfx_draw_string_scaled(cx - gfx_string_scaled_w("Orbital Motion", 3) / 2,
                           40, "Orbital Motion", GFX_WHITE, 3);

    /* Central glow */
    for (int r = 50; r > 0; r -= 2) {
        int a = 10 + (50 - r) * 3;
        if (a > 200) a = 200;
        gfx_fill_circle_aa(cx, cy, r, GFX_RGB(a/3, a/4, a));
    }

    /* Orbit rings */
    for (int ring = 0; ring < 4; ring++) {
        int radius = 100 + ring * 70;
        gfx_circle_ring(cx, cy, radius, 1, GFX_RGB(40, 45, 60));
    }

    /* Orbiting bodies with trails */
    for (int i = 0; i < 6; i++) {
        int radius = 100 + (i % 4) * 70;
        int speed = 3 + i * 2;
        int angle = frame * speed + i * 170;
        int hue = (i * 170) & 1023;
        uint32_t col = hsv_to_rgb(hue, 220, 255);

        /* Trail: 8 ghost positions */
        for (int t = 7; t >= 0; t--) {
            int ta = angle - t * speed * 2;
            int tx = cx + icos(ta) * radius / 244;
            int ty = cy + isin(ta) * radius / 244;
            int tr = 8 - t;
            int alpha = (8 - t) * 18;
            gfx_fill_circle_aa(tx, ty, tr, color_lerp(GFX_BLACK, col, alpha));
        }

        /* Main body */
        int bx = cx + icos(angle) * radius / 244;
        int by = cy + isin(angle) * radius / 244;
        gfx_fill_circle_aa(bx, by, 10, col);
        gfx_fill_circle_aa(bx - 2, by - 2, 4, GFX_WHITE);
    }

    /* FPS counter */
    char fps_str[32];
    snprintf(fps_str, sizeof(fps_str), "frame %d", frame);
    gfx_draw_string(10, H - 20, fps_str, GFX_RGB(100, 100, 100), GFX_BLACK);
}

/* Scene 2: Particle fountain */
#define DEMO_MAX_PARTICLES 120
static struct { int x, y, vx, vy; uint32_t col; int life; } particles[DEMO_MAX_PARTICLES];
static int particle_init_done = 0;

static void demo_scene_particles(int W, int H, int frame) {
    demo_draw_bg(W, H, frame);

    gfx_draw_string_scaled(W / 2 - gfx_string_scaled_w("Particle System", 3) / 2,
                           40, "Particle System", GFX_WHITE, 3);

    if (!particle_init_done) {
        memset(particles, 0, sizeof(particles));
        particle_init_done = 1;
    }

    /* Spawn new particles from center-bottom */
    for (int i = 0; i < DEMO_MAX_PARTICLES; i++) {
        if (particles[i].life <= 0) {
            particles[i].x = (W / 2) * 256;
            particles[i].y = (H - 120) * 256;
            int spread = (frame * 7 + i * 31) % 512 - 256;
            particles[i].vx = spread;
            particles[i].vy = -600 - ((frame * 13 + i * 17) % 400);
            particles[i].col = hsv_to_rgb((frame * 4 + i * 40) & 1023, 240, 255);
            particles[i].life = 60 + (i * 7) % 40;
            break;
        }
    }

    /* Update and draw */
    for (int i = 0; i < DEMO_MAX_PARTICLES; i++) {
        if (particles[i].life <= 0) continue;
        particles[i].x += particles[i].vx;
        particles[i].y += particles[i].vy;
        particles[i].vy += 10; /* gravity */
        particles[i].life--;

        int px = particles[i].x / 256;
        int py = particles[i].y / 256;
        int sz = 2 + particles[i].life / 20;
        int fade = particles[i].life * 255 / 100;
        if (fade > 255) fade = 255;

        uint32_t c = color_lerp(GFX_BLACK, particles[i].col, fade);
        gfx_fill_circle_aa(px, py, sz, c);
    }

    /* Emitter glow */
    for (int r = 30; r > 0; r -= 3) {
        int a = (30 - r) * 6;
        uint32_t gc = hsv_to_rgb((frame * 6) & 1023, 200, a > 255 ? 255 : a);
        gfx_fill_circle_aa(W / 2, H - 120, r, gc);
    }
}

/* Scene 3: Card showcase (rounded rects, alpha, smooth text) */
static void demo_scene_cards(int W, int H, int frame) {
    demo_draw_bg(W, H, frame);

    gfx_draw_string_scaled(W / 2 - gfx_string_scaled_w("Modern UI", 3) / 2,
                           40, "Modern UI", GFX_WHITE, 3);

    /* Floating cards */
    struct { const char *title; const char *sub; uint32_t accent; } cards[] = {
        { "Graphics",  "Shapes & AA", GFX_RGB(88, 166, 255) },
        { "Alpha",     "Transparency", GFX_RGB(255, 120, 88) },
        { "Smooth",    "SDF Fonts",   GFX_RGB(88, 255, 166) },
        { "Animate",   "60+ FPS",     GFX_RGB(200, 130, 255) },
    };

    int card_w = 280, card_h = 320, gap = 40;
    int total_w = 4 * card_w + 3 * gap;
    int start_x = (W - total_w) / 2;
    int base_y = 120;

    for (int i = 0; i < 4; i++) {
        /* Floating animation: each card bobs independently */
        int bob = isin(frame * 4 + i * 256) * 15 / 244;
        int cx = start_x + i * (card_w + gap);
        int cy = base_y + bob;

        /* Card shadow */
        gfx_rounded_rect_alpha(cx + 6, cy + 8, card_w, card_h, 16,
                               GFX_RGB(0, 0, 0), 80);

        /* Card body */
        gfx_rounded_rect(cx, cy, card_w, card_h, 16, GFX_RGB(30, 33, 40));
        gfx_rounded_rect_outline(cx, cy, card_w, card_h, 16, GFX_RGB(55, 60, 75));

        /* Accent bar at top */
        gfx_fill_rect(cx + 20, cy + 16, card_w - 40, 4, cards[i].accent);

        /* Icon area: animated circle */
        int icon_cx = cx + card_w / 2;
        int icon_cy = cy + 90;
        int pulse = 30 + isin(frame * 6 + i * 200) * 8 / 244;
        gfx_fill_circle_aa(icon_cx, icon_cy, pulse, cards[i].accent);
        gfx_fill_circle_aa(icon_cx, icon_cy, pulse - 8, GFX_RGB(30, 33, 40));

        /* Spinning ring */
        int ring_r = pulse + 12;
        gfx_circle_ring(icon_cx, icon_cy, ring_r, 2, cards[i].accent);

        /* Orbiting dot */
        int dot_a = frame * 8 + i * 256;
        int dot_x = icon_cx + icos(dot_a) * ring_r / 244;
        int dot_y = icon_cy + isin(dot_a) * ring_r / 244;
        gfx_fill_circle_aa(dot_x, dot_y, 5, GFX_WHITE);

        /* Title text */
        int tw = gfx_string_scaled_w(cards[i].title, 2);
        gfx_draw_string_scaled(icon_cx - tw / 2, cy + 160,
                               cards[i].title, GFX_WHITE, 2);

        /* Subtitle */
        int sw = gfx_string_scaled_w(cards[i].sub, 1);
        gfx_draw_string(icon_cx - sw / 2, cy + 200,
                         cards[i].sub, GFX_RGB(140, 145, 160), GFX_RGB(30, 33, 40));

        /* Faux progress bar */
        int bar_y = cy + 240;
        int bar_w = card_w - 60;
        int bar_x = cx + 30;
        gfx_rounded_rect(bar_x, bar_y, bar_w, 8, 4, GFX_RGB(45, 48, 58));
        int fill_w = (isin(frame * 3 + i * 300) + 244) * bar_w / 488;
        if (fill_w < 8) fill_w = 8;
        gfx_rounded_rect(bar_x, bar_y, fill_w, 8, 4, cards[i].accent);

        /* Bottom stat numbers */
        char stat[16];
        int pct = (isin(frame * 3 + i * 300) + 244) * 100 / 488;
        snprintf(stat, sizeof(stat), "%d%%", pct);
        gfx_draw_string(bar_x + bar_w + 8, bar_y - 4, stat,
                         cards[i].accent, GFX_RGB(30, 33, 40));
    }
}

/* Scene 4: Wave visualizer */
static void demo_scene_waves(int W, int H, int frame) {
    demo_draw_bg(W, H, frame);

    gfx_draw_string_scaled(W / 2 - gfx_string_scaled_w("Wave Synthesis", 3) / 2,
                           40, "Wave Synthesis", GFX_WHITE, 3);

    int cy = H / 2;

    /* Draw 5 layered waves */
    for (int wave = 0; wave < 5; wave++) {
        int amp = 60 - wave * 8;
        int freq = 3 + wave;
        int speed = 4 + wave * 2;
        uint32_t col = hsv_to_rgb((wave * 200 + frame * 3) & 1023, 200, 220);
        int prev_y = cy;

        for (int x = 0; x < W; x += 2) {
            int angle = x * freq + frame * speed;
            int y = cy + isin(angle) * amp / 244 +
                    isin(angle * 2 + frame * 3) * (amp / 3) / 244;

            /* Fill from wave to bottom with alpha */
            int fill_h = H - y;
            if (fill_h > 0) {
                uint32_t fill_col = GFX_RGBA(
                    (col >> 16) & 0xFF,
                    (col >> 8) & 0xFF,
                    col & 0xFF,
                    20 + wave * 10);
                gfx_fill_rect_alpha(x, y, 2, fill_h > 200 ? 200 : fill_h, fill_col);
            }

            /* Wave line */
            if (x > 0)
                gfx_draw_line(x - 2, prev_y, x, y, col);
            prev_y = y;
        }
    }

    /* Pulsing center orb */
    int orb_r = 40 + isin(frame * 8) * 15 / 244;
    for (int r = orb_r; r > 0; r -= 2) {
        int brightness = (orb_r - r) * 200 / orb_r;
        gfx_fill_circle_aa(W / 2, cy, r,
                           GFX_RGB(brightness / 2, brightness, brightness));
    }

    /* Frequency bars at bottom */
    int bar_count = 32;
    int bar_w = (W - 100) / bar_count;
    int bar_base = H - 80;
    for (int i = 0; i < bar_count; i++) {
        int bh = 20 + (isin(frame * 6 + i * 32) + 244) * 40 / 488;
        uint32_t bc = hsv_to_rgb((i * 32 + frame * 4) & 1023, 240, 230);
        int bx = 50 + i * bar_w;
        gfx_rounded_rect(bx, bar_base - bh, bar_w - 2, bh, 3, bc);
    }
}

static void cmd_gfxdemo(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    if (!gfx_is_active()) {
        printf("Graphics mode not available (text mode fallback)\n");
        return;
    }

    int W = (int)gfx_width();
    int H = (int)gfx_height();

    /* Suspend WM compositing so the demo owns the framebuffer */
    keyboard_set_idle_callback(0);

    particle_init_done = 0;
    int scene = 0;
    int frame = 0;
    uint32_t start_tick = pit_get_ticks();
    int total_scenes = 4;
    int demo_tid = task_register("gfxdemo", 1, -1);

    while (1) {
        uint32_t frame_start = pit_get_ticks();

        /* Draw current scene */
        switch (scene) {
            case 0: demo_scene_orbits(W, H, frame); break;
            case 1: demo_scene_particles(W, H, frame); break;
            case 2: demo_scene_cards(W, H, frame); break;
            case 3: demo_scene_waves(W, H, frame); break;
        }

        /* Scene indicator dots */
        int dot_y = H - 40;
        int dot_cx = W / 2;
        for (int i = 0; i < total_scenes; i++) {
            int dx = dot_cx + (i - total_scenes / 2) * 24 + 12;
            if (i == scene) {
                gfx_fill_circle_aa(dx, dot_y, 6, GFX_WHITE);
            } else {
                gfx_circle_ring(dx, dot_y, 6, 2, GFX_RGB(100, 100, 110));
            }
        }

        /* Help text */
        gfx_draw_string(W / 2 - 140, H - 20,
                         "SPACE: next scene  Q: quit",
                         GFX_RGB(120, 125, 140), GFX_BLACK);

        gfx_flip();
        frame++;

        /* Auto-advance scene every ~8 seconds (960 ticks at 120Hz) */
        if ((pit_get_ticks() - start_tick) > 960) {
            scene = (scene + 1) % total_scenes;
            start_tick = pit_get_ticks();
            particle_init_done = 0;
        }

        /* Check for killed flag */
        if (demo_tid >= 0 && task_check_killed(demo_tid)) break;

        /* Check for input (non-blocking) */
        if (keyboard_data_available()) {
            char c = getchar();
            if (c == 'q' || c == 'Q' || c == 27) break;
            if (c == ' ' || c == '\n') {
                scene = (scene + 1) % total_scenes;
                start_tick = pit_get_ticks();
                particle_init_done = 0;
            }
        }

        /* Cap at ~30fps: wait for at least 4 ticks */
        while (pit_get_ticks() - frame_start < 4) {
            task_set_current(TASK_IDLE);
            cpu_halting = 1;
            __asm__ volatile ("hlt");
            cpu_halting = 0;
        }
        task_set_current(TASK_SHELL);
    }

    /* Unregister process */
    if (demo_tid >= 0) task_unregister(demo_tid);

    /* Restore idle callback, then full WM composite to redraw desktop */
    keyboard_set_idle_callback(desktop_get_idle_terminal_cb());
    terminal_clear();
    if (gfx_is_active())
        wm_composite();
}

static void cmd_nslookup(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: nslookup HOSTNAME\n");
        return;
    }

    uint8_t ip[4];
    if (dns_resolve(argv[1], ip) == 0) {
        printf("%s: %d.%d.%d.%d\n", argv[1], ip[0], ip[1], ip[2], ip[3]);
    } else {
        printf("nslookup: could not resolve %s\n", argv[1]);
    }
}

static void cmd_dhcp_cmd(int argc, char* argv[]) {
    (void)argc; (void)argv;
    dhcp_discover();
}

static void cmd_httpd(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: httpd start|stop\n");
        return;
    }
    if (strcmp(argv[1], "start") == 0) {
        httpd_start();
    } else if (strcmp(argv[1], "stop") == 0) {
        httpd_stop();
    } else {
        printf("Usage: httpd start|stop\n");
    }
}

static void cmd_quota(int argc, char* argv[]) {
    if (argc >= 5 && strcmp(argv[1], "-s") == 0) {
        /* Set quota: quota -s UID MAX_INODES MAX_BLOCKS */
        uint16_t uid = atoi(argv[2]);
        uint16_t max_inodes = atoi(argv[3]);
        uint16_t max_blocks = atoi(argv[4]);
        if (quota_set(uid, max_inodes, max_blocks) == 0) {
            printf("Quota set for uid %d: max_inodes=%d max_blocks=%d\n",
                   uid, max_inodes, max_blocks);
        } else {
            printf("quota: failed to set quota\n");
        }
        return;
    }

    if (argc >= 3 && strcmp(argv[1], "-u") == 0) {
        /* Show quota for specific user */
        uint16_t uid = atoi(argv[2]);
        quota_entry_t* q = quota_get(uid);
        if (q) {
            printf("Quota for uid %d:\n", uid);
            printf("  Inodes: %d / %d\n", q->used_inodes,
                   q->max_inodes ? q->max_inodes : 0);
            printf("  Blocks: %d / %d\n", q->used_blocks,
                   q->max_blocks ? q->max_blocks : 0);
        } else {
            printf("No quota set for uid %d\n", uid);
        }
        return;
    }

    /* Default: show current user quota */
    uint16_t uid = user_get_current_uid();
    const char* uname = user_get_current();
    if (!uname) {
        printf("No current user\n");
        return;
    }
    quota_entry_t* q = quota_get(uid);
    if (q) {
        printf("Quota for %s (uid %d):\n", uname, uid);
        printf("  Inodes: %d / %d\n", q->used_inodes,
               q->max_inodes ? q->max_inodes : 0);
        printf("  Blocks: %d / %d\n", q->used_blocks,
               q->max_blocks ? q->max_blocks : 0);
    } else {
        printf("No quota set for %s\n", uname);
    }
}

static void cmd_connect(int argc, char* argv[]) {
    (void)argc; (void)argv;
    net_config_t* cfg = net_get_config();
    if (!cfg->link_up) {
        printf("connect: no network interface available\n");
        return;
    }

    printf("Running DHCP discovery...\n");
    if (dhcp_discover() == 0) {
        /* Re-fetch config after DHCP updated it */
        cfg = net_get_config();
        printf("Network configured:\n");
        printf("  IP:      "); net_print_ip(cfg->ip); printf("\n");
        printf("  Netmask: "); net_print_ip(cfg->netmask); printf("\n");
        printf("  Gateway: "); net_print_ip(cfg->gateway); printf("\n");
    } else {
        printf("connect: DHCP discovery failed\n");
    }
}

static int parse_ip(const char *s, uint8_t ip[4]) {
    int a, b, c, d;
    const char *p = s;
    a = atoi(p); while (*p && *p != '.') p++; if (*p) p++;
    b = atoi(p); while (*p && *p != '.') p++; if (*p) p++;
    c = atoi(p); while (*p && *p != '.') p++; if (*p) p++;
    d = atoi(p);
    if (a < 0 || a > 255 || b < 0 || b > 255 || c < 0 || c > 255 || d < 0 || d > 255)
        return -1;
    ip[0] = a; ip[1] = b; ip[2] = c; ip[3] = d;
    return 0;
}

static void cmd_firewall(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: firewall list|add|del|flush|default\n");
        return;
    }

    if (strcmp(argv[1], "list") == 0) {
        int n = firewall_rule_count();
        printf("Default policy: %s\n", firewall_get_default() == FW_ACTION_ALLOW ? "ALLOW" : "DENY");
        if (n == 0) {
            printf("No rules.\n");
            return;
        }
        printf("%-4s %-6s %-5s %-16s %-12s\n", "Idx", "Action", "Proto", "Src IP", "Dst Port");
        for (int i = 0; i < n; i++) {
            const fw_rule_t *r = firewall_get_rule(i);
            if (!r) continue;
            const char *act = r->action == FW_ACTION_ALLOW ? "ALLOW" : "DENY";
            const char *proto = "all";
            if (r->protocol == FW_PROTO_TCP)  proto = "tcp";
            if (r->protocol == FW_PROTO_UDP)  proto = "udp";
            if (r->protocol == FW_PROTO_ICMP) proto = "icmp";

            char src[32] = "any";
            uint8_t zero[4] = {0,0,0,0};
            if (memcmp(r->src_ip, zero, 4) != 0 || memcmp(r->src_mask, zero, 4) != 0) {
                snprintf(src, sizeof(src), "%d.%d.%d.%d",
                         r->src_ip[0], r->src_ip[1], r->src_ip[2], r->src_ip[3]);
            }

            char port[20] = "any";
            if (r->dst_port_max > 0) {
                if (r->dst_port_min == r->dst_port_max)
                    snprintf(port, sizeof(port), "%d", r->dst_port_min);
                else
                    snprintf(port, sizeof(port), "%d-%d", r->dst_port_min, r->dst_port_max);
            }

            printf("%-4d %-6s %-5s %-16s %-12s\n", i, act, proto, src, port);
        }
    } else if (strcmp(argv[1], "add") == 0) {
        if (argc < 4) {
            printf("Usage: firewall add allow|deny tcp|udp|icmp|all [src_ip[/mask]] [port[-port]]\n");
            return;
        }
        fw_rule_t rule;
        memset(&rule, 0, sizeof(rule));
        rule.enabled = 1;

        if (strcmp(argv[2], "allow") == 0)      rule.action = FW_ACTION_ALLOW;
        else if (strcmp(argv[2], "deny") == 0)   rule.action = FW_ACTION_DENY;
        else { printf("firewall: action must be allow or deny\n"); return; }

        if (strcmp(argv[3], "tcp") == 0)        rule.protocol = FW_PROTO_TCP;
        else if (strcmp(argv[3], "udp") == 0)    rule.protocol = FW_PROTO_UDP;
        else if (strcmp(argv[3], "icmp") == 0)   rule.protocol = FW_PROTO_ICMP;
        else if (strcmp(argv[3], "all") == 0)    rule.protocol = FW_PROTO_ALL;
        else { printf("firewall: protocol must be tcp, udp, icmp, or all\n"); return; }

        for (int i = 4; i < argc; i++) {
            /* Check if it's an IP with optional mask */
            char *slash = strchr(argv[i], '/');
            if (slash && strchr(argv[i], '.')) {
                *slash = '\0';
                if (parse_ip(argv[i], rule.src_ip) != 0) {
                    printf("firewall: bad IP '%s'\n", argv[i]);
                    return;
                }
                if (parse_ip(slash + 1, rule.src_mask) != 0) {
                    printf("firewall: bad mask '%s'\n", slash + 1);
                    return;
                }
            } else if (strchr(argv[i], '.')) {
                if (parse_ip(argv[i], rule.src_ip) != 0) {
                    printf("firewall: bad IP '%s'\n", argv[i]);
                    return;
                }
                memset(rule.src_mask, 255, 4);
            } else if (strcmp(argv[i], "all") == 0 || strcmp(argv[i], "any") == 0) {
                /* Leave src as 0.0.0.0/0.0.0.0 = any */
            } else {
                /* Port or port range */
                char *dash = strchr(argv[i], '-');
                if (dash) {
                    *dash = '\0';
                    rule.dst_port_min = (uint16_t)atoi(argv[i]);
                    rule.dst_port_max = (uint16_t)atoi(dash + 1);
                } else {
                    rule.dst_port_min = (uint16_t)atoi(argv[i]);
                    rule.dst_port_max = rule.dst_port_min;
                }
            }
        }

        if (firewall_add_rule(&rule) == 0)
            printf("Rule added (%d/%d)\n", firewall_rule_count(), FW_MAX_RULES);
        else
            printf("firewall: rule table full\n");

    } else if (strcmp(argv[1], "del") == 0) {
        if (argc < 3) { printf("Usage: firewall del INDEX\n"); return; }
        int idx = atoi(argv[2]);
        if (firewall_del_rule(idx) == 0)
            printf("Rule %d deleted\n", idx);
        else
            printf("firewall: invalid index\n");

    } else if (strcmp(argv[1], "flush") == 0) {
        firewall_flush();
        printf("All rules flushed\n");

    } else if (strcmp(argv[1], "default") == 0) {
        if (argc < 3) { printf("Usage: firewall default allow|deny\n"); return; }
        if (strcmp(argv[2], "allow") == 0)
            firewall_set_default(FW_ACTION_ALLOW);
        else if (strcmp(argv[2], "deny") == 0)
            firewall_set_default(FW_ACTION_DENY);
        else { printf("firewall: must be allow or deny\n"); return; }
        printf("Default policy: %s\n", argv[2]);

    } else {
        printf("Usage: firewall list|add|del|flush|default\n");
    }
}

static void cmd_kill(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: kill [-9|-INT|-TERM|-KILL|-USR1|-USR2|-PIPE] PID\n");
        return;
    }

    int pid;
    int signum = SIGTERM;  /* default */

    if (argv[1][0] == '-') {
        if (argc < 3) {
            printf("Usage: kill [-9|-INT|-TERM|-KILL|-USR1|-USR2|-PIPE] PID\n");
            return;
        }
        const char *s = argv[1] + 1;
        if (strcmp(s, "9") == 0 || strcmp(s, "KILL") == 0)
            signum = SIGKILL;
        else if (strcmp(s, "INT") == 0)
            signum = SIGINT;
        else if (strcmp(s, "TERM") == 0)
            signum = SIGTERM;
        else if (strcmp(s, "USR1") == 0)
            signum = SIGUSR1;
        else if (strcmp(s, "USR2") == 0)
            signum = SIGUSR2;
        else if (strcmp(s, "PIPE") == 0)
            signum = SIGPIPE;
        else {
            printf("kill: unknown signal '%s'\n", s);
            return;
        }
        pid = atoi(argv[2]);
    } else {
        pid = atoi(argv[1]);
    }

    int rc = sig_send_pid(pid, signum);
    if (rc == 0)
        printf("Sent signal %d to process %d\n", signum, pid);
    else if (rc == -2)
        printf("kill: cannot signal system process (PID %d)\n", pid);
    else
        printf("kill: no such process (PID %d)\n", pid);
}

/* ── Helpers for colored output ── */
#define TOP_C_HEADER  VGA_COLOR_LIGHT_CYAN
#define TOP_C_VALUE   VGA_COLOR_WHITE
#define TOP_C_LABEL   VGA_COLOR_LIGHT_GREY
#define TOP_C_BAR_FG  VGA_COLOR_LIGHT_GREEN
#define TOP_C_BAR_BG  VGA_COLOR_DARK_GREY
#define TOP_C_RUN     VGA_COLOR_LIGHT_GREEN
#define TOP_C_SLEEP   VGA_COLOR_LIGHT_GREY
#define TOP_C_IDLE_C  VGA_COLOR_LIGHT_BLUE
#define TOP_C_BG      VGA_COLOR_BLACK

static void top_bar(int pct, int width) {
    int fill = pct * width / 100;
    if (fill > width) fill = width;
    terminal_setcolor(TOP_C_BAR_FG, TOP_C_BG);
    printf("[");
    for (int i = 0; i < width; i++) {
        if (i < fill) printf("|");
        else { terminal_setcolor(TOP_C_BAR_BG, TOP_C_BG); printf("."); terminal_setcolor(TOP_C_BAR_FG, TOP_C_BG); }
    }
    terminal_setcolor(TOP_C_LABEL, TOP_C_BG);
    printf("]");
}

/* ── Non-blocking top: foreground app callbacks ── */
static int top_tid = -1;
static int top_first_render = 1;

static void top_render(void) {
    task_set_current(top_tid);

    terminal_clear();
    if (top_first_render && gfx_is_active()) {
        desktop_draw_chrome();
        top_first_render = 0;
    }

    /* ═══ Header ════════════════════════════════════════════ */
    datetime_t dt;
    config_get_datetime(&dt);
    uint32_t up_secs = pit_get_ticks() / 120;
    uint32_t up_h = up_secs / 3600;
    uint32_t up_m = (up_secs % 3600) / 60;
    uint32_t up_s = up_secs % 60;

    terminal_setcolor(TOP_C_HEADER, TOP_C_BG);
    printf("top");
    terminal_setcolor(TOP_C_LABEL, TOP_C_BG);
    printf(" - %02d:%02d:%02d up ",
           (int)dt.hour, (int)dt.minute, (int)dt.second);
    terminal_setcolor(TOP_C_VALUE, TOP_C_BG);
    printf("%d:%02d:%02d", (int)up_h, (int)up_m, (int)up_s);
    terminal_setcolor(TOP_C_LABEL, TOP_C_BG);
    printf(",  1 user\n\n");

    /* ═══ CPU bar ═══════════════════════════════════════════ */
    task_info_t *idle_t = task_get(TASK_IDLE);
    int user_x10 = 0, sys_x10 = 0, idle_x10 = 0;
    if (idle_t && idle_t->sample_total > 0)
        idle_x10 = (int)(idle_t->prev_ticks * 1000 / idle_t->sample_total);
    for (int i = 1; i < TASK_MAX; i++) {
        task_info_t *t = task_get(i);
        if (!t) continue;
        int pct_x10 = t->sample_total > 0
            ? (int)(t->prev_ticks * 1000 / t->sample_total) : 0;
        if (t->killable) user_x10 += pct_x10;
        else sys_x10 += pct_x10;
    }
    int cpu_pct = (1000 - idle_x10) / 10;
    if (cpu_pct < 0) cpu_pct = 0;

    terminal_setcolor(TOP_C_HEADER, TOP_C_BG);
    printf("CPU  ");
    top_bar(cpu_pct, 30);
    terminal_setcolor(TOP_C_VALUE, TOP_C_BG);
    printf(" %2d%%", cpu_pct);
    terminal_setcolor(TOP_C_LABEL, TOP_C_BG);
    printf("  (");
    terminal_setcolor(TOP_C_VALUE, TOP_C_BG);
    printf("%d.%d", user_x10 / 10, user_x10 % 10);
    terminal_setcolor(TOP_C_LABEL, TOP_C_BG);
    printf(" us, ");
    terminal_setcolor(TOP_C_VALUE, TOP_C_BG);
    printf("%d.%d", sys_x10 / 10, sys_x10 % 10);
    terminal_setcolor(TOP_C_LABEL, TOP_C_BG);
    printf(" sy, ");
    terminal_setcolor(TOP_C_VALUE, TOP_C_BG);
    printf("%d.%d", idle_x10 / 10, idle_x10 % 10);
    terminal_setcolor(TOP_C_LABEL, TOP_C_BG);
    printf(" id)\n");

    /* ═══ Memory bar ════════════════════════════════════════ */
    uint32_t ram_mb = gfx_get_system_ram_mb();
    size_t h_used = heap_used();
    size_t h_total = heap_total();
    size_t h_free = h_total > h_used ? h_total - h_used : 0;
    int used_mib_x10 = (int)(h_used / (1024 * 1024 / 10));
    int free_mib_x10 = (int)(h_free / (1024 * 1024 / 10));
    int mem_pct = (int)(h_used * 100 / h_total);

    terminal_setcolor(TOP_C_HEADER, TOP_C_BG);
    printf("Mem  ");
    top_bar(mem_pct, 30);
    terminal_setcolor(TOP_C_VALUE, TOP_C_BG);
    printf(" %d.%d", used_mib_x10 / 10, used_mib_x10 % 10);
    terminal_setcolor(TOP_C_LABEL, TOP_C_BG);
    printf("MiB / ");
    terminal_setcolor(TOP_C_VALUE, TOP_C_BG);
    printf("%d.0", (int)ram_mb);
    terminal_setcolor(TOP_C_LABEL, TOP_C_BG);
    printf("MiB  (");
    terminal_setcolor(TOP_C_VALUE, TOP_C_BG);
    printf("%d.%d", free_mib_x10 / 10, free_mib_x10 % 10);
    terminal_setcolor(TOP_C_LABEL, TOP_C_BG);
    printf("MiB free)\n");

    /* ═══ Disk + Net ════════════════════════════════════════ */
    int used_inodes = 0, used_blocks = 0;
    for (int i = 0; i < NUM_INODES; i++) {
        inode_t tmp;
        if (fs_read_inode(i, &tmp) == 0 && tmp.type != INODE_FREE) {
            used_inodes++;
            used_blocks += tmp.num_blocks;
            if (tmp.indirect_block) used_blocks++;
        }
    }
    uint32_t rd_ops = 0, rd_bytes = 0, wr_ops = 0, wr_bytes = 0;
    fs_get_io_stats(&rd_ops, &rd_bytes, &wr_ops, &wr_bytes);
    uint32_t tx_p = 0, tx_b = 0, rx_p = 0, rx_b = 0;
    net_get_stats(&tx_p, &tx_b, &rx_p, &rx_b);

    terminal_setcolor(TOP_C_HEADER, TOP_C_BG);
    printf("Disk ");
    terminal_setcolor(TOP_C_LABEL, TOP_C_BG);
    printf("%d/%d inodes  %d/%d blocks (%dKB)  ",
           used_inodes, NUM_INODES, used_blocks, NUM_BLOCKS,
           (int)(used_blocks * BLOCK_SIZE / 1024));
    terminal_setcolor(TOP_C_LABEL, TOP_C_BG);
    printf("R:");
    terminal_setcolor(TOP_C_VALUE, TOP_C_BG);
    printf("%d", (int)rd_ops);
    terminal_setcolor(TOP_C_LABEL, TOP_C_BG);
    printf(" W:");
    terminal_setcolor(TOP_C_VALUE, TOP_C_BG);
    printf("%d\n", (int)wr_ops);

    terminal_setcolor(TOP_C_HEADER, TOP_C_BG);
    printf("Net  ");
    terminal_setcolor(TOP_C_LABEL, TOP_C_BG);
    printf("TX: ");
    terminal_setcolor(TOP_C_VALUE, TOP_C_BG);
    printf("%d", (int)tx_p);
    terminal_setcolor(TOP_C_LABEL, TOP_C_BG);
    printf(" pkts (%dKB)  RX: ", (int)(tx_b / 1024));
    terminal_setcolor(TOP_C_VALUE, TOP_C_BG);
    printf("%d", (int)rx_p);
    terminal_setcolor(TOP_C_LABEL, TOP_C_BG);
    printf(" pkts (%dKB)\n", (int)(rx_b / 1024));

    /* ═══ GPU / Display ═════════════════════════════════════ */
    {
        int gpu_pct = (int)wm_get_gpu_usage();
        terminal_setcolor(TOP_C_HEADER, TOP_C_BG);
        printf("GPU  ");
        top_bar(gpu_pct, 30);
        terminal_setcolor(TOP_C_VALUE, TOP_C_BG);
        printf(" %2d%%", gpu_pct);
        terminal_setcolor(TOP_C_LABEL, TOP_C_BG);
        printf("  FPS:");
        terminal_setcolor(TOP_C_VALUE, TOP_C_BG);
        printf("%d", (int)wm_get_fps());
        terminal_setcolor(TOP_C_LABEL, TOP_C_BG);
        printf("  %dx%dx%d", (int)gfx_width(), (int)gfx_height(), (int)gfx_bpp());
        printf("  VRAM:");
        terminal_setcolor(TOP_C_VALUE, TOP_C_BG);
        printf("%dKB\n", (int)(gfx_width() * gfx_height() * (gfx_bpp() / 8) / 1024));
    }

    /* ═══ Task counts ═══════════════════════════════════════ */
    int n_total = 0, n_running = 0, n_sleeping = 0, n_idle = 0;
    for (int i = 0; i < TASK_MAX; i++) {
        task_info_t *t = task_get(i);
        if (!t) continue;
        n_total++;
        if (i == TASK_IDLE) { n_idle++; continue; }
        int pct = t->sample_total > 0
            ? (int)(t->prev_ticks * 100 / t->sample_total) : 0;
        if (pct > 0) n_running++;
        else n_sleeping++;
    }

    printf("\n");
    terminal_setcolor(TOP_C_HEADER, TOP_C_BG);
    printf("Tasks: ");
    terminal_setcolor(TOP_C_VALUE, TOP_C_BG);
    printf("%d", n_total);
    terminal_setcolor(TOP_C_LABEL, TOP_C_BG);
    printf(" total, ");
    terminal_setcolor(TOP_C_RUN, TOP_C_BG);
    printf("%d running", n_running);
    terminal_setcolor(TOP_C_LABEL, TOP_C_BG);
    printf(", ");
    terminal_setcolor(TOP_C_SLEEP, TOP_C_BG);
    printf("%d sleeping", n_sleeping);
    terminal_setcolor(TOP_C_LABEL, TOP_C_BG);
    printf(", ");
    terminal_setcolor(TOP_C_IDLE_C, TOP_C_BG);
    printf("%d idle", n_idle);
    terminal_setcolor(TOP_C_LABEL, TOP_C_BG);
    printf("\n\n");

    /* ═══ Process table header ══════════════════════════════ */
    const char *cur_user = user_get_current();
    if (!cur_user) cur_user = "root";

    terminal_setcolor(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
    printf("  %5s %-8s S %%CPU %%GPU    RES     TIME+ COMMAND      \n",
           "PID", "USER");
    terminal_setcolor(TOP_C_LABEL, TOP_C_BG);

    /* ═══ Process rows (sorted by CPU desc) ═════════════════ */
    int indices[TASK_MAX];
    int count = 0;
    for (int i = 0; i < TASK_MAX; i++) {
        if (task_get(i)) indices[count++] = i;
    }
    for (int i = 1; i < count; i++) {
        int key = indices[i];
        task_info_t *kt = task_get(key);
        int kpct = kt->sample_total > 0
            ? (int)(kt->prev_ticks * 1000 / kt->sample_total) : 0;
        int j = i - 1;
        while (j >= 0) {
            task_info_t *jt = task_get(indices[j]);
            int jpct = jt->sample_total > 0
                ? (int)(jt->prev_ticks * 1000 / jt->sample_total) : 0;
            if (jpct >= kpct) break;
            indices[j + 1] = indices[j];
            j--;
        }
        indices[j + 1] = key;
    }

    for (int n = 0; n < count; n++) {
        int i = indices[n];
        task_info_t *t = task_get(i);
        if (!t) continue;

        int task_cpu_x10 = t->sample_total > 0
            ? (int)(t->prev_ticks * 1000 / t->sample_total) : 0;

        char state;
        enum vga_color row_color;
        if (i == TASK_IDLE) { state = 'I'; row_color = TOP_C_IDLE_C; }
        else if (task_cpu_x10 > 0) { state = 'R'; row_color = TOP_C_RUN; }
        else { state = 'S'; row_color = TOP_C_SLEEP; }

        uint32_t tticks = t->total_ticks;
        uint32_t tsecs = tticks / 120;
        uint32_t tcs = (tticks % 120) * 100 / 120;
        uint32_t tmins = tsecs / 60;
        uint32_t ts = tsecs % 60;
        const char *uname = t->killable ? cur_user : "root";

        char res_str[12];
        if (t->mem_kb > 0)
            snprintf(res_str, sizeof(res_str), "%dK", t->mem_kb);
        else
            strcpy(res_str, "0K");

        int gpu_pct = t->gpu_sample_total > 0
            ? (int)(t->gpu_prev_ticks * 100 / t->gpu_sample_total) : 0;

        terminal_setcolor(TOP_C_LABEL, TOP_C_BG);
        printf("  %5d ", task_get_pid(i));
        terminal_setcolor(t->killable ? TOP_C_VALUE : VGA_COLOR_LIGHT_RED, TOP_C_BG);
        printf("%-8s ", uname);
        terminal_setcolor(row_color, TOP_C_BG);
        printf("%c ", state);
        terminal_setcolor(task_cpu_x10 > 0 ? TOP_C_VALUE : TOP_C_LABEL, TOP_C_BG);
        printf("%2d.%d ", task_cpu_x10 / 10, task_cpu_x10 % 10);
        terminal_setcolor(gpu_pct > 0 ? GFX_RGB(80, 180, 255) : TOP_C_LABEL, TOP_C_BG);
        printf("%3d ", gpu_pct);
        terminal_setcolor(t->mem_kb > 0 ? TOP_C_HEADER : TOP_C_LABEL, TOP_C_BG);
        printf("%6s ", res_str);
        terminal_setcolor(TOP_C_LABEL, TOP_C_BG);
        printf(" %4d:%02d.%02d ", (int)tmins, (int)ts, (int)tcs);
        terminal_setcolor(TOP_C_VALUE, TOP_C_BG);
        printf("%-s\n", t->name);
    }

    terminal_setcolor(TOP_C_LABEL, TOP_C_BG);
    printf("\nPress 'q' to quit, refreshes every 1s\n");
    terminal_resetcolor();

    wm_composite();
}

static void top_on_key(char c) {
    if (c == 'q' || c == 'Q') {
        if (top_tid >= 0) task_unregister(top_tid);
        top_tid = -1;
        shell_unregister_fg_app();
        terminal_resetcolor();
        terminal_clear();
        if (gfx_is_active()) desktop_draw_chrome();
        shell_draw_prompt();
        wm_composite();
    }
}

static void top_on_tick(void) {
    if (top_tid >= 0 && task_check_killed(top_tid)) {
        top_on_key('q');
        return;
    }
    top_render();
}

static void top_on_close(void) {
    if (top_tid >= 0) task_unregister(top_tid);
    top_tid = -1;
    shell_unregister_fg_app();
    terminal_resetcolor();
}

static shell_fg_app_t top_fg_app = {
    .on_key = top_on_key,
    .on_tick = top_on_tick,
    .on_close = top_on_close,
    .tick_interval = 100,
    .task_id = -1,
};

static void cmd_top(int argc, char* argv[]) {
    (void)argc; (void)argv;
    top_tid = task_register("top", 1, -1);
    top_fg_app.task_id = top_tid;
    top_first_render = 1;
    top_render();
    shell_register_fg_app(&top_fg_app);
}

/* ═══ display — real-time FPS and input monitor ═══════════════ */

static void cmd_display(int argc, char* argv[]) {
    (void)argc; (void)argv;

    if (!gfx_is_active()) {
        printf("Graphics mode not available\n");
        return;
    }

    int W = (int)gfx_width();
    int H = (int)gfx_height();

    keyboard_set_idle_callback(0);
    int tid = task_register("display", 1, -1);

    uint32_t frame = 0;
    uint32_t fps = 0;
    uint32_t sec_start = pit_get_ticks();
    uint32_t frames_this_sec = 0;
    uint32_t fps_min = 0, fps_max = 0;
    uint32_t fps_accum = 0, fps_samples = 0;

    while (1) {
        uint32_t now = pit_get_ticks();

        /* Calculate FPS every second (120 ticks) */
        if (now - sec_start >= 120) {
            fps = frames_this_sec * 120 / (now - sec_start);
            if (fps_samples == 0 || fps < fps_min) fps_min = fps;
            if (fps > fps_max) fps_max = fps;
            fps_accum += fps;
            fps_samples++;
            frames_this_sec = 0;
            sec_start = now;
        }

        /* Background */
        gfx_clear(GFX_RGB(18, 20, 28));

        /* Animated ring of dots — light load to show real frame rate */
        int cx = W / 2, cy = H / 2 + 60;
        for (int i = 0; i < 12; i++) {
            int angle = (int)frame * 3 + i * 85;
            int rx = cx + isin(angle) * 180 / 244;
            int ry = cy + icos(angle) * 180 / 244;
            uint32_t c = hsv_to_rgb((i * 85 + (int)frame * 4) & 1023, 200, 220);
            gfx_fill_circle(rx, ry, 12, c);
        }

        char buf[128];

        /* FPS — big centered */
        snprintf(buf, sizeof(buf), "FPS: %u", (unsigned)fps);
        gfx_draw_string_scaled(cx - gfx_string_scaled_w(buf, 5) / 2,
                               50, buf, GFX_WHITE, 5);

        /* Min / Avg / Max */
        snprintf(buf, sizeof(buf), "MIN: %u   AVG: %u   MAX: %u",
                 (unsigned)(fps_samples > 0 ? fps_min : 0),
                 (unsigned)(fps_samples > 0 ? fps_accum / fps_samples : 0),
                 (unsigned)fps_max);
        gfx_draw_string_scaled(cx - gfx_string_scaled_w(buf, 2) / 2,
                               140, buf, GFX_RGB(180, 185, 200), 2);

        /* Mouse info */
        int mx = mouse_get_x(), my = mouse_get_y();
        uint8_t mb = mouse_get_buttons();
        snprintf(buf, sizeof(buf), "Mouse: %d, %d   Buttons: %c %c %c",
                 mx, my,
                 (mb & 1) ? 'L' : '-',
                 (mb & 4) ? 'M' : '-',
                 (mb & 2) ? 'R' : '-');
        gfx_draw_string_scaled(cx - gfx_string_scaled_w(buf, 2) / 2,
                               190, buf, GFX_RGB(160, 165, 180), 2);

        /* Crosshair at mouse position (drawn to backbuffer) */
        gfx_draw_line(mx - 30, my, mx - 6, my, GFX_RGB(255, 60, 60));
        gfx_draw_line(mx + 6, my, mx + 30, my, GFX_RGB(255, 60, 60));
        gfx_draw_line(mx, my - 30, mx, my - 6, GFX_RGB(255, 60, 60));
        gfx_draw_line(mx, my + 6, mx, my + 30, GFX_RGB(255, 60, 60));
        gfx_fill_circle(mx, my, 3, GFX_RGB(255, 60, 60));

        /* Frame counter + help */
        snprintf(buf, sizeof(buf), "Frame: %u", (unsigned)frame);
        gfx_draw_string(cx - (int)strlen(buf) * FONT_W / 2, H - 60,
                         buf, GFX_RGB(100, 105, 120), GFX_RGB(18, 20, 28));
        gfx_draw_string(cx - 11 * FONT_W / 2, H - 30,
                         "Q: quit", GFX_RGB(80, 85, 100), GFX_RGB(18, 20, 28));

        gfx_flip();
        frame++;
        frames_this_sec++;

        if (tid >= 0 && task_check_killed(tid)) break;

        int key = keyboard_getchar_nb();
        if (key == 'q' || key == 'Q' || key == 27) break;

        /* No frame cap — measure true rendering throughput */
    }

    if (tid >= 0) task_unregister(tid);
    keyboard_set_idle_callback(desktop_get_idle_terminal_cb());
    terminal_clear();
    if (gfx_is_active()) wm_composite();
}

/* ═══ gfxbench — max-throughput rendering stress test ═════════ */

static uint32_t bench_seed;
static uint32_t bench_brand(void) {
    bench_seed = bench_seed * 1103515245 + 12345;
    return (bench_seed >> 16) & 0x7FFF;
}

static void cmd_gfxbench(int argc, char* argv[]) {
    (void)argc; (void)argv;

    if (!gfx_is_active()) {
        printf("Graphics mode not available\n");
        return;
    }

    int W = (int)gfx_width();
    int H = (int)gfx_height();

    keyboard_set_idle_callback(0);
    int tid = task_register("gfxbench", 1, -1);

    bench_seed = pit_get_ticks() ^ 0xDEADBEEF;
    uint32_t frame = 0;
    uint32_t fps = 0;
    uint32_t sec_start = pit_get_ticks();
    uint32_t frames_this_sec = 0;
    int phase = 0;
    uint32_t phase_start = pit_get_ticks();
    int total_phases = 5;
    uint32_t phase_fps[5] = {0};
    uint32_t phase_pixels_ok[5] = {0};
    uint32_t phase_frames[5] = {0};
    int quit = 0;

    static const char *phase_names[] = {
        "Rect Flood", "Line Storm", "Circle Cascade",
        "Alpha Blend", "Combined Chaos"
    };

    while (!quit) {
        uint32_t now = pit_get_ticks();

        /* FPS every second */
        if (now - sec_start >= 120) {
            fps = frames_this_sec * 120 / (now - sec_start);
            phase_fps[phase] = fps;
            frames_this_sec = 0;
            sec_start = now;
        }

        /* Auto-advance phase every 5 seconds (600 ticks at 120Hz) */
        if (now - phase_start >= 600) {
            phase++;
            if (phase >= total_phases) break;
            phase_start = now;
        }

        gfx_clear(GFX_BLACK);

        /* Inline PRNG */
        #define BRAND() bench_brand()

        switch (phase) {
        case 0: /* Rect flood — 200 random filled rects */
            for (int i = 0; i < 200; i++) {
                int rx = (int)(BRAND() % (unsigned)W);
                int ry = (int)(BRAND() % (unsigned)H);
                int rw = 10 + (int)(BRAND() % 200);
                int rh = 10 + (int)(BRAND() % 200);
                uint32_t c = hsv_to_rgb(((int)BRAND() + (int)frame * 7) & 1023, 200, 220);
                gfx_fill_rect(rx, ry, rw, rh, c);
            }
            break;

        case 1: /* Line storm — 500 random lines */
            for (int i = 0; i < 500; i++) {
                int x0 = (int)(BRAND() % (unsigned)W);
                int y0 = (int)(BRAND() % (unsigned)H);
                int x1 = (int)(BRAND() % (unsigned)W);
                int y1 = (int)(BRAND() % (unsigned)H);
                uint32_t c = hsv_to_rgb(((int)BRAND() + (int)frame * 5) & 1023, 240, 255);
                gfx_draw_line(x0, y0, x1, y1, c);
            }
            break;

        case 2: /* Circle cascade — 100 random circles */
            for (int i = 0; i < 100; i++) {
                int ccx = (int)(BRAND() % (unsigned)W);
                int ccy = (int)(BRAND() % (unsigned)H);
                int r = 5 + (int)(BRAND() % 80);
                uint32_t c = hsv_to_rgb((i * 10 + (int)frame * 8) & 1023, 220, 240);
                gfx_fill_circle(ccx, ccy, r, c);
            }
            break;

        case 3: /* Alpha blend stress — 150 overlapping translucent rects */
            for (int i = 0; i < 150; i++) {
                int rx = (int)(BRAND() % (unsigned)W);
                int ry = (int)(BRAND() % (unsigned)H);
                int rw = 20 + (int)(BRAND() % 300);
                int rh = 20 + (int)(BRAND() % 300);
                uint32_t c = hsv_to_rgb(((int)BRAND() + (int)frame * 3) & 1023, 200, 200);
                uint8_t a = (uint8_t)(60 + BRAND() % 140);
                gfx_fill_rect_alpha(rx, ry, rw, rh,
                    GFX_RGBA((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, a));
            }
            break;

        case 4: /* Combined chaos — rects + lines + circles */
            for (int i = 0; i < 80; i++) {
                int rx = (int)(BRAND() % (unsigned)W);
                int ry = (int)(BRAND() % (unsigned)H);
                int rw = 10 + (int)(BRAND() % 150);
                int rh = 10 + (int)(BRAND() % 150);
                gfx_fill_rect(rx, ry, rw, rh,
                    hsv_to_rgb(((int)BRAND() + (int)frame * 6) & 1023, 200, 200));
            }
            for (int i = 0; i < 200; i++) {
                gfx_draw_line(
                    (int)(BRAND() % (unsigned)W), (int)(BRAND() % (unsigned)H),
                    (int)(BRAND() % (unsigned)W), (int)(BRAND() % (unsigned)H),
                    hsv_to_rgb(((int)BRAND() + (int)frame * 4) & 1023, 240, 255));
            }
            for (int i = 0; i < 30; i++) {
                gfx_fill_circle(
                    (int)(BRAND() % (unsigned)W), (int)(BRAND() % (unsigned)H),
                    5 + (int)(BRAND() % 50),
                    hsv_to_rgb(((int)BRAND() + (int)frame * 9) & 1023, 220, 230));
            }
            break;
        }

        #undef BRAND

        /* HUD bar */
        gfx_fill_rect(0, 0, W, 50, GFX_RGB(0, 0, 0));
        char buf[128];
        snprintf(buf, sizeof(buf), "Phase %d/%d: %s   FPS: %u   Frame: %u",
                 phase + 1, total_phases, phase_names[phase],
                 (unsigned)fps, (unsigned)frame);
        gfx_draw_string(10, 8, buf, GFX_WHITE, GFX_BLACK);

        /* Progress bar */
        int elapsed = (int)(now - phase_start);
        int bar_w = W - 20;
        int fill = elapsed * bar_w / 600;
        if (fill > bar_w) fill = bar_w;
        gfx_fill_rect(10, 34, bar_w, 8, GFX_RGB(40, 40, 50));
        gfx_fill_rect(10, 34, fill, 8, GFX_RGB(80, 160, 255));

        gfx_draw_string(W - 18 * FONT_W, 8,
                         "Q: quit early", GFX_RGB(120, 125, 140), GFX_BLACK);

        gfx_flip();
        frame++;
        frames_this_sec++;

        if (tid >= 0 && task_check_killed(tid)) break;

        int key = keyboard_getchar_nb();
        if (key == 'q' || key == 'Q' || key == 27) { quit = 1; break; }

        /* Validation: sample a pixel from backbuffer after drawing */
        {
            uint32_t *bb = gfx_backbuffer();
            int sx = W / 2, sy = H / 2;
            uint32_t px = bb[sy * (int)(gfx_pitch() / 4) + sx];
            if (px != 0) phase_pixels_ok[phase]++;
            phase_frames[phase]++;
        }

        /* No frame cap — maximum throughput */
    }

    if (tid >= 0) task_unregister(tid);
    keyboard_set_idle_callback(desktop_get_idle_terminal_cb());
    terminal_clear();

    /* Print results summary with validation */
    printf("=== Graphics Benchmark Results ===\n");
    int all_pass = 1;
    for (int i = 0; i < total_phases; i++) {
        if (i > phase) break;
        int drawn = (phase_frames[i] > 0 && phase_pixels_ok[i] > 0);
        int rate_ok = (phase_fps[i] > 0);
        int pass = drawn && rate_ok;
        if (!pass) all_pass = 0;
        printf("  %-18s %4u fps  %5u frames  %s\n",
               phase_names[i], (unsigned)phase_fps[i],
               (unsigned)phase_frames[i],
               pass ? "PASS" : "FAIL");
    }
    printf("  Total frames: %u\n", (unsigned)frame);
    printf("  Result: %s\n", all_pass ? "ALL PASS" : "SOME FAILED");
    printf("==================================\n");

    if (gfx_is_active()) wm_composite();
}

/* ═══ fps — toggle FPS overlay on desktop ═════════════════════ */

static void cmd_fps(int argc, char* argv[]) {
    (void)argc; (void)argv;
    wm_toggle_fps();
    printf("FPS overlay: %s\n", wm_fps_enabled() ? "ON" : "OFF");
    wm_composite();
}

/* ═══ Spawn: background thread test commands ═══════════════════ */

static void thread_counter(void) {
    int tid = task_get_current();
    int pid = task_get_pid(tid);
    for (int i = 0; ; i++) {
        printf("[thread %d] count = %d\n", pid, i);
        pit_sleep_ms(1000);
    }
}

static void thread_hog(void) {
    volatile uint32_t x = 0;
    while (1) x++;
}

/* Ring 3 user-mode counter thread.
 * Uses INT 0x80 syscalls instead of kernel functions for sleep/getpid. */
static void user_thread_counter(void) {
    int pid;
    __asm__ volatile (
        "mov $3, %%eax\n\t"    /* SYS_GETPID */
        "int $0x80\n\t"
        "mov %%eax, %0"
        : "=r"(pid) : : "eax"
    );

    for (int i = 0; ; i++) {
        printf("[user %d] count = %d\n", pid, i);
        __asm__ volatile (
            "mov $2, %%eax\n\t"    /* SYS_SLEEP */
            "mov $1000, %%ebx\n\t" /* 1000ms */
            "int $0x80\n\t"
            : : : "eax", "ebx"
        );
    }
}

static void cmd_spawn(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: spawn [counter|hog|user-counter]\n");
        return;
    }

    if (strcmp(argv[1], "user-counter") == 0) {
        int tid = task_create_user_thread("user-counter", user_thread_counter, 1);
        if (tid < 0) {
            printf("spawn: failed to create user thread (no free slots)\n");
            return;
        }
        int pid = task_get_pid(tid);
        printf("[User Thread %d] user-counter started (PID %d, ring 3)\n", tid, pid);
        return;
    }

    void (*entry)(void) = 0;
    const char *name = argv[1];

    if (strcmp(argv[1], "counter") == 0)
        entry = thread_counter;
    else if (strcmp(argv[1], "hog") == 0)
        entry = thread_hog;
    else {
        printf("spawn: unknown thread type '%s'\n", argv[1]);
        printf("  Available: counter, hog, user-counter\n");
        return;
    }

    int tid = task_create_thread(name, entry, 1);
    if (tid < 0) {
        printf("spawn: failed to create thread (no free slots)\n");
        return;
    }
    int pid = task_get_pid(tid);
    printf("[Thread %d] %s started (PID %d)\n", tid, name, pid);
}

/* ═══ shm: shared memory management ═══════════════════════════ */

static void cmd_shm(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: shm [list|create NAME SIZE]\n");
        return;
    }

    if (strcmp(argv[1], "list") == 0) {
        printf("ID  Name                 Pages  Refs\n");
        printf("--  -------------------  -----  ----\n");
        int found = 0;
        shm_region_t *regions = shm_get_regions();
        for (int i = 0; i < SHM_MAX_REGIONS; i++) {
            if (regions[i].active) {
                printf("%-3d %-20s %-6d %d\n",
                       i, regions[i].name, regions[i].num_pages, regions[i].ref_count);
                found++;
            }
        }
        if (!found)
            printf("(no shared memory regions)\n");

    } else if (strcmp(argv[1], "create") == 0) {
        if (argc < 4) {
            printf("Usage: shm create NAME SIZE\n");
            return;
        }
        uint32_t size = (uint32_t)atoi(argv[3]);
        int id = shm_create(argv[2], size);
        if (id >= 0)
            printf("Created shared memory '%s' (id=%d, %d bytes, %d pages)\n",
                   argv[2], id, (int)size, (int)((size + 4095) / 4096));
        else
            printf("shm: failed to create region '%s'\n", argv[2]);

    } else {
        printf("Usage: shm [list|create NAME SIZE]\n");
    }
}

/* ═══ ntpdate: sync time via NTP ════════════════════════════════ */

static void cmd_ntpdate(int argc, char* argv[]) {
    (void)argc; (void)argv;
    printf("Syncing time via NTP (pool.ntp.org)...\n");
    if (rtc_ntp_sync() == 0) {
        datetime_t dt;
        config_get_datetime(&dt);
        printf("Time synchronized: %04d-%02d-%02d %02d:%02d:%02d\n",
               dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
    } else {
        printf("NTP sync failed (check network connection)\n");
    }
}

/* ═══ beep: PC speaker test ════════════════════════════════════ */

static void cmd_beep(int argc, char* argv[]) {
    if (argc >= 3) {
        uint32_t freq = (uint32_t)atoi(argv[1]);
        uint32_t dur  = (uint32_t)atoi(argv[2]);
        if (freq > 0 && dur > 0) {
            beep(freq, dur);
            return;
        }
    }
    if (argc == 2 && strcmp(argv[1], "startup") == 0) { beep_startup(); return; }
    if (argc == 2 && strcmp(argv[1], "error") == 0)   { beep_error(); return; }
    if (argc == 2 && strcmp(argv[1], "ok") == 0)      { beep_ok(); return; }
    if (argc == 2 && strcmp(argv[1], "notify") == 0)   { beep_notify(); return; }
    /* Default beep */
    beep(880, 150);
}

static void cmd_run(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: run <file>\n");
        return;
    }

    /* Try ELF first (elf_run reads the file internally and validates) */
    int ret = elf_run_argv(argv[1], argc - 1, (const char **)&argv[1]);
    if (ret >= 0) {
        task_info_t *t = task_get(ret);
        printf("Started ELF process '%s' (PID %d)\n", argv[1],
               t ? t->pid : ret);
        while (t && t->active && t->state != TASK_STATE_ZOMBIE)
            task_yield();
        return;
    }

    /* Try PE */
    DBG("cmd_run: calling pe_run('%s')", argv[1]);
    ret = pe_run(argv[1]);
    DBG("cmd_run: pe_run returned %d", ret);
    if (ret < 0) {
        printf("Failed to run '%s' (error %d)\n", argv[1], ret);
    } else {
        for (int i = 0; i < 5; i++) task_yield();
    }
}

/* ── Embedded hello.exe (2048 bytes) ──────────────────────────
 * Minimal PE32 console app: imports puts() from msvcrt.dll,
 * prints "Hello from Win32!", calls ExitProcess(0).
 * Includes .reloc section with base relocations.
 * Generated by tools/gen_test_exe.c
 */
static const uint8_t hello_exe_data[] = {
  0x4d, 0x5a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x40, 0x00, 0x00, 0x00, 0x50, 0x45, 0x00, 0x00, 0x4c, 0x01, 0x03, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xe0, 0x00, 0x02, 0x01, 0x0b, 0x01, 0x01, 0x00, 0x00, 0x02, 0x00, 0x00,
  0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
  0x00, 0x10, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00,
  0x00, 0x10, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x50, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x10, 0x00, 0x00,
  0x00, 0x00, 0x10, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x20, 0x00, 0x00, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00,
  0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x70, 0x20, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x2e, 0x74, 0x65, 0x78, 0x74, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
  0x00, 0x10, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x20, 0x00, 0x00, 0x60, 0x2e, 0x72, 0x64, 0x61, 0x74, 0x61, 0x00, 0x00,
  0x00, 0x02, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
  0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x40, 0x2e, 0x72, 0x65, 0x6c,
  0x6f, 0x63, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00,
  0x00, 0x02, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x42,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x68, 0x98, 0x20, 0x40,
  0x00, 0xff, 0x15, 0x70, 0x20, 0x40, 0x00, 0x83, 0xc4, 0x04, 0x6a, 0x00,
  0xff, 0x15, 0x78, 0x20, 0x40, 0x00, 0xcc, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x60, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x40, 0x20, 0x00, 0x00, 0x70, 0x20, 0x00, 0x00,
  0x68, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x50, 0x20, 0x00, 0x00, 0x78, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6d, 0x73, 0x76, 0x63,
  0x72, 0x74, 0x2e, 0x64, 0x6c, 0x6c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x6b, 0x65, 0x72, 0x6e, 0x65, 0x6c, 0x33, 0x32, 0x2e, 0x64, 0x6c, 0x6c,
  0x00, 0x00, 0x00, 0x00, 0x80, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x88, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x88, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x70, 0x75, 0x74, 0x73, 0x00, 0x00, 0x00, 0x00, 0x45, 0x78,
  0x69, 0x74, 0x50, 0x72, 0x6f, 0x63, 0x65, 0x73, 0x73, 0x00, 0x00, 0x00,
  0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x66, 0x72, 0x6f, 0x6d, 0x20, 0x57,
  0x69, 0x6e, 0x33, 0x32, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x10, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x01, 0x30, 0x07, 0x30,
  0x12, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const unsigned int hello_exe_len = 2048;

static void cmd_petest(int argc, char* argv[]) {
    (void)argc; (void)argv;
    fs_create_file("hello.exe", 0);
    fs_write_file("hello.exe", hello_exe_data, hello_exe_len);
    int ret = pe_run("hello.exe");
    if (ret >= 0)
        for (int i = 0; i < 5; i++) task_yield();
}

/* ── Embedded hello_gui.exe (Win32 GUI test app) ─────────────
 * PE32 GUI app: imports from user32.dll and gdi32.dll.
 * Creates a window with text and colored rectangles.
 * Generated by: i686-w64-mingw32-gcc -Os -nostdlib -Wl,--strip-all
 */
#include "hello_gui_embed.h"

static void cmd_petest_gui(int argc, char* argv[]) {
    (void)argc; (void)argv;
    fs_create_file("hello_gui.exe", 0);
    fs_write_file("hello_gui.exe", hello_gui_data, hello_gui_data_len);
    int ret = pe_run("hello_gui.exe");
    if (ret >= 0) {
        for (int i = 0; i < 5; i++) task_yield();
    } else {
        printf("petest-gui: failed (%d)\n", ret);
    }
}

/* ── Embedded thread_test.exe (Win32 threading test) ──────────
 * PE32 console app: tests CreateThread, CriticalSection, Events,
 * and Interlocked operations.
 * Generated by: i686-w64-mingw32-gcc -Os -nostdlib -Wl,--subsystem,console
 */
#include "thread_test_embed.h"

static void cmd_threadtest(int argc, char* argv[]) {
    (void)argc; (void)argv;
    fs_create_file("thread_test.exe", 0);
    fs_write_file("thread_test.exe", thread_test_data, thread_test_data_len);
    int tid = pe_run("thread_test.exe");
    if (tid >= 0) {
        /* Wait for PE task to finish */
        while (task_get(tid) != NULL)
            task_yield();
    } else {
        printf("threadtest: failed (%d)\n", tid);
    }
}

/* ── Embedded mem_test.exe (Win32 memory test) ────────────────
 * PE32 console app: tests VirtualAlloc, VirtualProtect,
 * VirtualQuery, VirtualFree, and GlobalAlloc.
 */
#include "mem_test_embed.h"

static void cmd_memtest(int argc, char* argv[]) {
    (void)argc; (void)argv;
    fs_create_file("mem_test.exe", 0);
    fs_write_file("mem_test.exe", mem_test_data, mem_test_data_len);
    int tid = pe_run("mem_test.exe");
    if (tid >= 0) {
        while (task_get(tid) != NULL)
            task_yield();
    } else {
        printf("memtest: failed (%d)\n", tid);
    }
}

/* ── Embedded fs_test.exe (Win32 FS test) ─────────────────────
 * PE32 console app: tests CreateFile, ReadFile, WriteFile,
 * SetFilePointer, FindFirstFile, CopyFile, DeleteFile, paths.
 */
#include "fs_test_embed.h"

static void cmd_fstest(int argc, char* argv[]) {
    (void)argc; (void)argv;
    fs_create_file("fs_test.exe", 0);
    fs_write_file("fs_test.exe", fs_test_data, fs_test_data_len);
    int tid = pe_run("fs_test.exe");
    if (tid >= 0) {
        while (task_get(tid) != NULL)
            task_yield();
    } else {
        printf("fstest: failed (%d)\n", tid);
    }
}

/* ── Embedded proc_test.exe (Win32 process test) ──────────────
 * PE32 console app: tests CreateProcessA, WaitForSingleObject
 * on process, GetExitCodeProcess, CreatePipe, DuplicateHandle.
 */
#include "proc_test_embed.h"

static void cmd_proctest(int argc, char* argv[]) {
    (void)argc; (void)argv;
    /* Ensure hello.exe exists — proc_test spawns it as child */
    fs_create_file("hello.exe", 0);
    fs_write_file("hello.exe", hello_exe_data, hello_exe_len);
    /* Write proc_test.exe */
    fs_create_file("proc_test.exe", 0);
    fs_write_file("proc_test.exe", proc_test_data, proc_test_data_len);
    int tid = pe_run("proc_test.exe");
    if (tid >= 0) {
        while (task_get(tid) != NULL)
            task_yield();
    } else {
        printf("proctest: failed (%d)\n", tid);
    }
}

/* Ensure /apps directory exists, cd into it, do work, cd back */
static void winget_ensure_apps_dir(void) {
    uint32_t saved_cwd = fs_get_cwd_inode();
    /* Go to root */
    fs_change_directory("/");
    /* Try to enter apps — if it fails, create it */
    if (fs_change_directory("apps") < 0) {
        fs_create_file("apps", 1);  /* 1 = directory */
    }
    /* Restore cwd */
    fs_change_directory_by_inode(saved_cwd);
}

static void cmd_winget(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: winget <command> [args]\n");
        printf("\nCommands:\n");
        printf("  install <url>   Download and install a package\n");
        printf("  list            List installed packages\n");
        printf("  run <name>      Run an installed package\n");
        printf("  remove <name>   Remove an installed package\n");
        printf("  info            Show winget info\n");
        return;
    }

    if (strcmp(argv[1], "list") == 0) {
        winget_ensure_apps_dir();
        uint32_t saved_cwd = fs_get_cwd_inode();
        fs_change_directory("/");
        if (fs_change_directory("apps") < 0) {
            printf("No packages installed.\n");
            fs_change_directory_by_inode(saved_cwd);
            return;
        }

        printf("Name                         Size\n");
        printf("----------------------------------\n");
        fs_dir_entry_info_t entries[32];
        int n = fs_enumerate_directory(entries, 32, 0);
        int found = 0;
        for (int i = 0; i < n; i++) {
            if (entries[i].name[0] == '.') continue;
            printf("%-28s %u bytes\n", entries[i].name, (unsigned)entries[i].size);
            found++;
        }
        if (!found)
            printf("No packages installed.\n");

        fs_change_directory_by_inode(saved_cwd);

    } else if (strcmp(argv[1], "run") == 0) {
        if (argc < 3) {
            printf("Usage: winget run <name>\n");
            return;
        }

        winget_ensure_apps_dir();
        uint32_t saved_cwd = fs_get_cwd_inode();
        fs_change_directory("/");
        if (fs_change_directory("apps") < 0) {
            printf("winget: /apps not found\n");
            fs_change_directory_by_inode(saved_cwd);
            return;
        }

        printf("Running %s...\n", argv[2]);
        pe_run(argv[2]);

        fs_change_directory_by_inode(saved_cwd);

    } else if (strcmp(argv[1], "remove") == 0) {
        if (argc < 3) {
            printf("Usage: winget remove <name>\n");
            return;
        }

        winget_ensure_apps_dir();
        uint32_t saved_cwd = fs_get_cwd_inode();
        fs_change_directory("/");
        if (fs_change_directory("apps") < 0) {
            printf("winget: /apps not found\n");
            fs_change_directory_by_inode(saved_cwd);
            return;
        }

        if (fs_delete_file(argv[2]) < 0)
            printf("winget: '%s' not found\n", argv[2]);
        else
            printf("Removed %s\n", argv[2]);

        fs_change_directory_by_inode(saved_cwd);

    } else if (strcmp(argv[1], "install") == 0) {
        if (argc < 3) {
            printf("Usage: winget install <url>\n");
            printf("  Example: winget install https://example.com/app.exe\n");
            return;
        }

        const char *url = argv[2];

        if (memcmp(url, "https://", 8) != 0) {
            printf("winget: only https:// URLs are supported\n");
            return;
        }

        net_config_t *cfg = net_get_config();
        if (!cfg || !cfg->link_up || (cfg->ip[0] == 0 && cfg->ip[1] == 0)) {
            printf("winget: network not configured. Run 'dhcp' first.\n");
            return;
        }

        /* Parse URL: https://host/path */
        char host[256], path[256];
        const char *hp = url + 8;
        int hi = 0;
        while (*hp && *hp != '/' && hi < 255)
            host[hi++] = *hp++;
        host[hi] = '\0';

        if (*hp == '/') {
            int pi = 0;
            while (*hp && pi < 255)
                path[pi++] = *hp++;
            path[pi] = '\0';
        } else {
            path[0] = '/'; path[1] = '\0';
        }

        /* Extract filename from URL path */
        const char *filename = path;
        const char *s = path;
        while (*s) { if (*s == '/') filename = s + 1; s++; }
        if (!*filename) filename = "download.exe";

        printf("Downloading %s from %s...\n", filename, host);

        static https_async_t dl_req;
        strncpy(dl_req.host, host, 255); dl_req.host[255] = '\0';
        dl_req.port = 443;
        strncpy(dl_req.path, path, 255); dl_req.path[255] = '\0';

        if (https_get_async(&dl_req) < 0) {
            printf("winget: failed to start download\n");
            return;
        }

        while (!dl_req.done) {
            keyboard_run_idle();
            task_yield();
        }

        uint8_t *body = dl_req.body;
        size_t body_len = dl_req.body_len;
        if (dl_req.result < 0 || !body || body_len == 0) {
            printf("winget: download failed\n");
            if (body) free(body);
            return;
        }

        printf("Downloaded %u bytes\n", (unsigned)body_len);

        /* Save into /apps directory */
        winget_ensure_apps_dir();
        uint32_t saved_cwd = fs_get_cwd_inode();
        fs_change_directory("/");
        fs_change_directory("apps");

        fs_create_file(filename, 0);
        int ret = fs_write_file(filename, body, body_len);
        free(body);

        fs_change_directory_by_inode(saved_cwd);

        if (ret < 0) {
            printf("winget: failed to save %s (file too large?)\n", filename);
            return;
        }

        printf("Installed %s to /apps/%s\n", filename, filename);

        /* Run if it's an .exe */
        size_t flen = strlen(filename);
        if (flen > 4 && strcmp(filename + flen - 4, ".exe") == 0) {
            printf("Running %s...\n", filename);
            /* cd into /apps to run */
            saved_cwd = fs_get_cwd_inode();
            fs_change_directory("/");
            fs_change_directory("apps");
            pe_run(filename);
            fs_change_directory_by_inode(saved_cwd);
        }

    } else if (strcmp(argv[1], "info") == 0) {
        printf("ImposOS WinGet Package Manager v2.0 (TLS)\n");
        printf("Install directory: /apps\n");
        printf("Transport: HTTPS (TLS 1.2)\n");
        printf("Supported: PE32 executables (.exe)\n");
    } else {
        printf("winget: unknown command '%s'\n", argv[1]);
    }
}
