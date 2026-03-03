#include <kernel/shell_cmd.h>
#include <kernel/shell.h>
#include <kernel/sh_parse.h>
#include <kernel/user.h>
#include <kernel/group.h>
#include <kernel/fs.h>
#include <kernel/env.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

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

static const command_t user_commands[] = {
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
};

const command_t *cmd_user_commands(int *count) {
    *count = sizeof(user_commands) / sizeof(user_commands[0]);
    return user_commands;
}
