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
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

int shell_exit_requested = 0;

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
        "    Only the file owner or root can change permissions.\n"
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
        "    Only root can change file ownership.\n"
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
        "    The -s flag is required (only symlinks are supported).\n"
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
        "    Print the target of the symbolic link LINK.\n"
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
        "    the current user is root. Default target is root.\n"
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
        "    Print user and group information for the current user.\n"
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
        "    password, and home directory. Root only.\n"
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
        "    the user's home directory. Root only.\n"
    },
    {
        "test", cmd_test,
        "Run regression tests",
        "test: test\n"
        "    Run all regression test suites.\n",
        "NAME\n"
        "    test - run regression tests\n\n"
        "SYNOPSIS\n"
        "    test\n\n"
        "DESCRIPTION\n"
        "    Run all built-in test suites and print results.\n"
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
        "    The current user session is ended.\n"
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
        "    rectangles, lines, and text. Press any key to exit.\n"
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
        "    resolved IPv4 address.\n"
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
        "    to obtain a network configuration from the DHCP server.\n"
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
        "    Use 'httpd stop' to shut it down.\n"
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
        "    INODES and BLOCKS are maximum counts (0 = unlimited).\n"
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
        "    assigned configuration on success.\n"
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
        "    default  Set default policy to allow or deny.\n"
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
        "    Press 'q' to exit and return to the shell.\n"
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
    config_initialize();
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
                    uint8_t dir_data[MAX_DIRECT_SIZE];
                    size_t dir_size = dir_inode.size;
                    if (dir_size > MAX_DIRECT_SIZE) dir_size = MAX_DIRECT_SIZE;

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

    uint8_t* buffer = (uint8_t*)malloc(MAX_FILE_SIZE);
    if (!buffer) {
        printf("cat: out of memory\n");
        return;
    }
    size_t size;
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

static void cmd_test(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    test_run_all();
}

static void cmd_logout(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    config_save_history();
    config_save();
    fs_sync();
    printf("Logging out...\n");
    exit(0);
}

static void cmd_gfxdemo(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    if (!gfx_is_active()) {
        printf("Graphics mode not available (text mode fallback)\n");
        return;
    }

    uint32_t w = gfx_width();
    uint32_t h = gfx_height();

    /* Clear to dark blue background */
    gfx_clear(GFX_RGB(0, 0, 40));

    /* Draw border */
    gfx_draw_rect(2, 2, (int)w - 4, (int)h - 4, GFX_WHITE);

    /* Title */
    gfx_draw_string((int)w / 2 - 100, 20, "ImposOS GFX Demo", GFX_WHITE, GFX_RGB(0, 0, 40));
    char info[80];
    snprintf(info, sizeof(info), "Resolution: %dx%d  Grid: %dx%d",
             (int)w, (int)h, (int)gfx_cols(), (int)gfx_rows());
    gfx_draw_string((int)w / 2 - 120, 40, info, GFX_CYAN, GFX_RGB(0, 0, 40));

    /* Filled rectangles - color palette */
    int bx = 40, by = 80, bw = 60, bh = 40;
    uint32_t colors[] = {
        GFX_RED, GFX_GREEN, GFX_BLUE, GFX_CYAN,
        GFX_YELLOW, GFX_MAGENTA, GFX_WHITE, GFX_RGB(255, 128, 0)
    };
    const char* names[] = {
        "Red", "Green", "Blue", "Cyan",
        "Yellow", "Magenta", "White", "Orange"
    };
    for (int i = 0; i < 8; i++) {
        int x = bx + i * (bw + 10);
        gfx_fill_rect(x, by, bw, bh, colors[i]);
        gfx_draw_rect(x, by, bw, bh, GFX_WHITE);
        gfx_draw_string(x + 4, by + bh + 4, names[i], colors[i], GFX_RGB(0, 0, 40));
    }

    /* Diagonal lines - starburst pattern */
    int cx = (int)w / 2;
    int cy_base = 220;
    for (int angle = 0; angle < 360; angle += 15) {
        /* Simple angle approximation using integer math */
        int dx = 0, dy = 0;
        int r = 80;
        /* Use lookup for sin/cos approximation at key angles */
        switch (angle % 360) {
            case 0:   dx = r;    dy = 0;    break;
            case 15:  dx = r*97/100; dy = r*26/100; break;
            case 30:  dx = r*87/100; dy = r/2;      break;
            case 45:  dx = r*71/100; dy = r*71/100;  break;
            case 60:  dx = r/2;      dy = r*87/100;  break;
            case 75:  dx = r*26/100; dy = r*97/100;  break;
            case 90:  dx = 0;    dy = r;    break;
            case 105: dx = -r*26/100; dy = r*97/100; break;
            case 120: dx = -r/2;      dy = r*87/100; break;
            case 135: dx = -r*71/100; dy = r*71/100; break;
            case 150: dx = -r*87/100; dy = r/2;      break;
            case 165: dx = -r*97/100; dy = r*26/100; break;
            case 180: dx = -r;   dy = 0;    break;
            case 195: dx = -r*97/100; dy = -r*26/100; break;
            case 210: dx = -r*87/100; dy = -r/2;      break;
            case 225: dx = -r*71/100; dy = -r*71/100; break;
            case 240: dx = -r/2;      dy = -r*87/100; break;
            case 255: dx = -r*26/100; dy = -r*97/100; break;
            case 270: dx = 0;    dy = -r;   break;
            case 285: dx = r*26/100;  dy = -r*97/100; break;
            case 300: dx = r/2;       dy = -r*87/100; break;
            case 315: dx = r*71/100;  dy = -r*71/100; break;
            case 330: dx = r*87/100;  dy = -r/2;      break;
            case 345: dx = r*97/100;  dy = -r*26/100; break;
        }
        /* Color cycle */
        uint32_t lc = GFX_RGB(
            128 + (angle * 127 / 360),
            255 - (angle * 200 / 360),
            (angle * 255 / 360)
        );
        gfx_draw_line(cx, cy_base, cx + dx, cy_base + dy, lc);
    }

    /* Nested rectangles */
    int rx = (int)w - 250, ry = 160;
    for (int i = 0; i < 8; i++) {
        uint32_t rc = GFX_RGB(255 - i * 30, i * 30, 128);
        gfx_draw_rect(rx + i * 8, ry + i * 8, 120 - i * 16, 80 - i * 16, rc);
    }

    /* Alpha blending demo: overlapping semi-transparent rectangles */
    int ax = 40, ay = 340;
    gfx_fill_rect(ax, ay, 120, 80, GFX_RGB(200, 0, 0));
    gfx_fill_rect_alpha(ax + 40, ay + 20, 120, 80, GFX_RGBA(0, 0, 255, 128));
    gfx_fill_rect_alpha(ax + 80, ay + 10, 120, 80, GFX_RGBA(0, 200, 0, 100));
    gfx_draw_string(ax, ay + 84, "Alpha blending demo", GFX_WHITE, GFX_RGB(0, 0, 40));

    /* Gradient bar */
    int gy = (int)h - 100;
    for (int x = 40; x < (int)w - 40; x++) {
        int t = (x - 40) * 255 / ((int)w - 80);
        gfx_fill_rect(x, gy, 1, 30, GFX_RGB(t, 0, 255 - t));
    }
    gfx_draw_string(40, gy + 34, "Red <-- Gradient --> Blue", GFX_WHITE, GFX_RGB(0, 0, 40));

    /* Bottom message */
    gfx_draw_string((int)w / 2 - 80, (int)h - 30, "Press any key to exit...",
                    GFX_YELLOW, GFX_RGB(0, 0, 40));

    gfx_flip();

    /* Wait for keypress */
    getchar();

    /* Restore terminal */
    terminal_clear();
    if (gfx_is_active())
        desktop_draw_chrome();
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

static void cmd_top(int argc, char* argv[]) {
    (void)argc; (void)argv;

    /* Take initial CPU snapshot for delta calculation */
    uint32_t prev_idle = 0, prev_busy = 0;
    pit_get_cpu_stats(&prev_idle, &prev_busy);

    while (1) {
        terminal_clear();
        if (gfx_is_active()) desktop_draw_chrome();

        /* Header: uptime + user@hostname */
        uint32_t secs = pit_get_ticks() / 100;
        uint32_t hrs = secs / 3600;
        uint32_t mins = (secs % 3600) / 60;
        uint32_t sec = secs % 60;
        printf("top - %dh %dm %ds up, user: %s@%s\n",
               (int)hrs, (int)mins, (int)sec,
               user_get_current(), hostname_get());

        /* CPU usage via idle/busy delta */
        uint32_t cur_idle = 0, cur_busy = 0;
        pit_get_cpu_stats(&cur_idle, &cur_busy);
        uint32_t d_idle = cur_idle - prev_idle;
        uint32_t d_busy = cur_busy - prev_busy;
        uint32_t d_total = d_idle + d_busy;
        int cpu_pct = d_total > 0 ? (int)(d_busy * 100 / d_total) : 0;
        int cpu_bar = cpu_pct / 5;
        prev_idle = cur_idle;
        prev_busy = cur_busy;

        printf("CPU:   %3d%%  [", cpu_pct);
        for (int i = 0; i < 20; i++)
            printf(i < cpu_bar ? "#" : ".");
        printf("]\n");

        /* Heap memory */
        size_t used = heap_used();
        size_t total = heap_total();
        int pct = total > 0 ? (int)((uint64_t)used * 100 / total) : 0;
        int bar_fill = pct / 5;
        printf("Heap:  %dKB / %dKB  [", (int)(used / 1024), (int)(total / 1024));
        for (int i = 0; i < 20; i++)
            printf(i < bar_fill ? "#" : ".");
        printf("] %d%%\n", pct);
        printf("RAM:   %dMB physical\n", (int)gfx_get_system_ram_mb());

        /* FS stats */
        int used_inodes = 0;
        int used_blocks = 0;
        for (int i = 0; i < NUM_INODES; i++) {
            inode_t tmp;
            if (fs_read_inode(i, &tmp) == 0 && tmp.type != INODE_FREE) {
                used_inodes++;
                used_blocks += tmp.num_blocks;
                if (tmp.indirect_block)
                    used_blocks++;
            }
        }

        /* Disk line with I/O stats */
        uint32_t rd_ops = 0, rd_bytes = 0, wr_ops = 0, wr_bytes = 0;
        fs_get_io_stats(&rd_ops, &rd_bytes, &wr_ops, &wr_bytes);
        printf("Disk:  %d/%d inodes, %d/%d blocks (%dKB)  R: %d ops (%dKB)  W: %d ops (%dKB)\n",
               used_inodes, NUM_INODES,
               used_blocks, NUM_BLOCKS,
               (int)(used_blocks * BLOCK_SIZE / 1024),
               (int)rd_ops, (int)(rd_bytes / 1024),
               (int)wr_ops, (int)(wr_bytes / 1024));

        /* Net line with I/O stats */
        uint32_t tx_p = 0, tx_b = 0, rx_p = 0, rx_b = 0;
        net_get_stats(&tx_p, &tx_b, &rx_p, &rx_b);
        printf("Net:   TX: %d pkts (%dKB)  RX: %d pkts (%dKB)\n\n",
               (int)tx_p, (int)(tx_b / 1024),
               (int)rx_p, (int)(rx_b / 1024));

        /* Process table: kernel subsystems + windows */
        int pid = 0;
        int wcount = wm_get_window_count();
        net_config_t *ncfg = net_get_config();
        int net_up = ncfg && ncfg->link_up;

        /* Count total tasks */
        int total_tasks = 6;
        if (net_up) total_tasks++;
        if (gfx_is_active()) total_tasks++;
        total_tasks += wcount;

        printf("Tasks: %d total\n\n", total_tasks);
        printf("  %-4s %-16s %-10s %-9s %s\n", "PID", "NAME", "STATE", "MEM(KB)", "INFO");
        printf("  %-4s %-16s %-10s %-9s %s\n", "---", "----", "-----", "-------", "----");

        /* PID 0: kernel */
        printf("  %-4d %-16s %-10s %-9s %s\n", pid++, "kernel", "running", "-", "i386 multiboot");

        /* PID 1: pit_timer */
        {
            char info[48];
            snprintf(info, sizeof(info), "100Hz, %d ticks", (int)pit_get_ticks());
            printf("  %-4d %-16s %-10s %-9s %s\n", pid++, "pit_timer", "running", "-", info);
        }

        /* PID 2: keyboard */
        {
            const char *layout = keyboard_get_layout() == KB_LAYOUT_FR ? "FR" : "US";
            char info[32];
            snprintf(info, sizeof(info), "IRQ1, layout %s", layout);
            printf("  %-4d %-16s %-10s %-9s %s\n", pid++, "keyboard", "running", "-", info);
        }

        /* PID 3: mouse */
        printf("  %-4d %-16s %-10s %-9s %s\n", pid++, "mouse", "running", "-", "IRQ12, PS/2");

        /* PID 4: fs — memory = inode table + used data blocks */
        {
            int fs_mem = (int)((NUM_INODES * sizeof(inode_t) + (uint32_t)used_blocks * BLOCK_SIZE) / 1024);
            char mem[16];
            snprintf(mem, sizeof(mem), "%d", fs_mem);
            char info[48];
            snprintf(info, sizeof(info), "%d/%d inodes, %dKB",
                     used_inodes, NUM_INODES, (int)(used_blocks * BLOCK_SIZE / 1024));
            printf("  %-4d %-16s %-10s %-9s %s\n", pid++, "fs", "running", mem, info);
        }

        /* PID 5: net (if up) */
        if (net_up) {
            char info[48];
            snprintf(info, sizeof(info), "%d.%d.%d.%d",
                     ncfg->ip[0], ncfg->ip[1], ncfg->ip[2], ncfg->ip[3]);
            printf("  %-4d %-16s %-10s %-9s %s\n", pid++, "net", "running", "-", info);
        }

        /* WM (if active) — memory = framebuffer */
        if (gfx_is_active()) {
            int fb_kb = (int)(gfx_width() * gfx_height() * 4 / 1024);
            char mem[16];
            snprintf(mem, sizeof(mem), "%d", fb_kb);
            char info[48];
            snprintf(info, sizeof(info), "%d windows, fb=%dKB", wcount, fb_kb);
            printf("  %-4d %-16s %-10s %-9s %s\n", pid++, "wm", "running", mem, info);
        }

        /* PID N: shell */
        printf("  %-4d %-16s %-10s %-9s %s\n", pid++, "shell", "sleeping", "-", "waiting (top)");

        /* Windows as tasks */
        for (int i = 0; i < wcount; i++) {
            wm_window_t *w = wm_get_window_by_index(i);
            if (!w) continue;
            int win_mem = w->canvas_w * w->canvas_h * 4 / 1024;
            char mem[16];
            snprintf(mem, sizeof(mem), "%d", win_mem);
            char info[64];
            snprintf(info, sizeof(info), "%dx%d%s",
                     w->w, w->h,
                     (w->flags & WM_WIN_FOCUSED) ? " [focused]" : "");
            const char *state = (w->flags & WM_WIN_MINIMIZED) ? "suspended" : "running";
            printf("  %-4d %-16s %-10s %-9s %s\n", pid++, w->title, state, mem, info);
        }

        printf("\nPress 'q' to quit, refreshes every 1s\n");

        /* Wait ~1s, checking for 'q' every 100ms */
        for (int i = 0; i < 10; i++) {
            pit_sleep_ms(100);
            if (keyboard_data_available()) {
                char c = getchar();
                if (c == 'q' || c == 'Q') {
                    terminal_clear();
                    if (gfx_is_active()) desktop_draw_chrome();
                    return;
                }
            }
        }
    }
}
