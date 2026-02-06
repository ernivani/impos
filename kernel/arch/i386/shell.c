#include <kernel/shell.h>
#include <kernel/fs.h>
#include <kernel/tty.h>
#include <kernel/vi.h>
#include <string.h>
#include <stdio.h>

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
static void cmd_pwd(int argc, char* argv[]);
static void cmd_mkdir(int argc, char* argv[]);
static void cmd_rm(int argc, char* argv[]);
static void cmd_vi(int argc, char* argv[]);

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
};

#define NUM_COMMANDS (sizeof(commands) / sizeof(commands[0]))

void shell_initialize(void) {
    fs_initialize();
    printf("ImposOS Shell v2.0\n");
    printf("Type 'help' for a list of commands.\n");
    printf("Press Tab for command auto-completion.\n");
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
    if (prefix_len == 0) {
        // Nothing to complete
        return buffer_pos;
    }

    const char* first_match = NULL;
    size_t matches = 0;

    for (size_t i = 0; i < NUM_COMMANDS; i++) {
        const char* name = commands[i].name;

        // Manual prefix compare to avoid relying on strncmp from libc
        size_t j = 0;
        for (; j < prefix_len; j++) {
            if (name[j] == '\0' || name[j] != buffer[start + j]) {
                break;
            }
        }
        if (j == prefix_len) {
            if (matches == 0) {
                first_match = name;
            }
            matches++;
        }
    }

    if (matches != 1 || first_match == NULL) {
        // Either no match or ambiguous; leave buffer unchanged for now
        return buffer_pos;
    }

    size_t full_len = strlen(first_match);
    if (full_len <= prefix_len) {
        // Already complete
        return buffer_pos;
    }

    size_t to_add = full_len - prefix_len;
    if (buffer_pos + to_add >= buffer_size) {
        to_add = buffer_size - buffer_pos - 1; // keep room for terminator when used
    }

    memcpy(&buffer[buffer_pos], first_match + prefix_len, to_add);
    buffer_pos += to_add;

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
