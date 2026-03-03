#include <kernel/shell_cmd.h>
#include <kernel/sh_parse.h>
#include <kernel/fs.h>
#include <kernel/user.h>
#include <kernel/group.h>
#include <kernel/glob.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

static void cmd_cat(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: cat <filename>\n");
        return;
    }

    uint32_t parent;
    char name_buf[28];
    int ino = fs_resolve_path(argv[1], &parent, name_buf);
    if (ino < 0) {
        printf("cat: %s: No such file\n", argv[1]);
        return;
    }
    inode_t node;
    fs_read_inode(ino, &node);
    if (node.type != 1) {
        printf("cat: %s: Not a regular file\n", argv[1]);
        return;
    }
    if (node.size == 0) {
        return;
    }
    uint8_t* buffer = (uint8_t*)malloc(node.size);
    if (!buffer) {
        printf("cat: out of memory\n");
        return;
    }
    uint32_t offset = 0;
    while (offset < node.size) {
        int n = fs_read_at(ino, buffer + offset, offset, node.size - offset);
        if (n <= 0) break;
        offset += n;
    }
    for (uint32_t i = 0; i < offset; i++) {
        putchar(buffer[i]);
    }
    printf("\n");
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
    if (argc >= 4 && strcmp(argv[1], "-s") == 0) {
        /* Symbolic link: ln -s TARGET LINKNAME */
        if (fs_create_symlink(argv[2], argv[3]) != 0)
            printf("ln: cannot create symbolic link '%s'\n", argv[3]);
    } else if (argc >= 3 && argv[1][0] != '-') {
        /* Hard link: ln TARGET LINKNAME */
        if (fs_link(argv[1], argv[2]) != 0)
            printf("ln: cannot create hard link '%s'\n", argv[2]);
    } else {
        printf("Usage: ln [-s] TARGET LINKNAME\n");
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

static void cmd_cp(int argc, char* argv[]) {
    if (argc < 3) {
        printf("Usage: cp SOURCE DEST\n");
        sh_set_exit_code(1);
        return;
    }
    uint32_t parent;
    char name[28];
    int src_ino = fs_resolve_path(argv[1], &parent, name);
    if (src_ino < 0) {
        printf("cp: %s: No such file\n", argv[1]);
        sh_set_exit_code(1);
        return;
    }
    inode_t src_node;
    fs_read_inode(src_ino, &src_node);
    if (src_node.type != 1) {
        printf("cp: %s: Not a regular file\n", argv[1]);
        sh_set_exit_code(1);
        return;
    }
    /* Read source */
    uint8_t *buf = NULL;
    if (src_node.size > 0) {
        buf = (uint8_t *)malloc(src_node.size);
        if (!buf) {
            printf("cp: out of memory\n");
            sh_set_exit_code(1);
            return;
        }
        uint32_t offset = 0;
        while (offset < src_node.size) {
            int n = fs_read_at(src_ino, buf + offset, offset, src_node.size - offset);
            if (n <= 0) break;
            offset += n;
        }
    }
    /* Create and write destination */
    fs_create_file(argv[2], 0);
    int dst_ino = fs_resolve_path(argv[2], &parent, name);
    if (dst_ino < 0) {
        printf("cp: cannot create '%s'\n", argv[2]);
        if (buf) free(buf);
        sh_set_exit_code(1);
        return;
    }
    if (buf && src_node.size > 0) {
        fs_truncate_inode(dst_ino, 0);
        fs_write_at(dst_ino, buf, 0, src_node.size);
        free(buf);
    }
    sh_set_exit_code(0);
}

static void cmd_mv(int argc, char* argv[]) {
    if (argc < 3) {
        printf("Usage: mv SOURCE DEST\n");
        sh_set_exit_code(1);
        return;
    }
    /* Try rename first */
    if (fs_rename(argv[1], argv[2]) == 0) {
        sh_set_exit_code(0);
        return;
    }
    /* Fallback: copy + delete */
    char *cp_argv[] = { "cp", argv[1], argv[2] };
    cmd_cp(3, cp_argv);
    if (sh_get_exit_code() == 0) {
        fs_delete_file(argv[1]);
    }
}

static void find_recursive(const char *dir_path, const char *name_pattern, int depth) {
    if (depth > 10) return; /* prevent infinite recursion */

    uint32_t saved_cwd = fs_get_cwd_inode();
    if (fs_change_directory(dir_path) < 0)
        return;

    fs_dir_entry_info_t entries[128];
    int n = fs_enumerate_directory(entries, 128, 0);

    for (int i = 0; i < n; i++) {
        /* Skip . and .. */
        if (strcmp(entries[i].name, ".") == 0 || strcmp(entries[i].name, "..") == 0)
            continue;

        char full_path[256];
        if (strcmp(dir_path, "/") == 0)
            snprintf(full_path, sizeof(full_path), "/%s", entries[i].name);
        else
            snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entries[i].name);

        if (name_pattern) {
            if (glob_match(name_pattern, entries[i].name))
                printf("%s\n", full_path);
        } else {
            printf("%s\n", full_path);
        }

        /* Recurse into directories */
        if (entries[i].type == 2) { /* INODE_DIR */
            find_recursive(full_path, name_pattern, depth + 1);
            /* Restore CWD after recursion */
            fs_change_directory_by_inode(saved_cwd);
        }
    }

    fs_change_directory_by_inode(saved_cwd);
}

static void cmd_find(int argc, char* argv[]) {
    const char *path = ".";
    const char *name_pattern = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-name") == 0 && i + 1 < argc) {
            name_pattern = argv[++i];
        } else if (argv[i][0] != '-') {
            path = argv[i];
        }
    }

    /* Convert . to current directory path */
    char start_path[256];
    if (strcmp(path, ".") == 0) {
        extern const char *fs_get_cwd(void);
        const char *cwd = fs_get_cwd();
        strncpy(start_path, cwd, 255);
        start_path[255] = '\0';
    } else {
        strncpy(start_path, path, 255);
        start_path[255] = '\0';
    }

    find_recursive(start_path, name_pattern, 0);
    sh_set_exit_code(0);
}

static const command_t fs_commands[] = {
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
        "cp", cmd_cp,
        "Copy files",
        "cp: cp SOURCE DEST\n"
        "    Copy SOURCE file to DEST.\n",
        NULL, 0
    },
    {
        "mv", cmd_mv,
        "Move/rename files",
        "mv: mv SOURCE DEST\n"
        "    Move or rename SOURCE to DEST.\n",
        NULL, 0
    },
    {
        "find", cmd_find,
        "Search for files in directory tree",
        "find: find [PATH] [-name PATTERN]\n"
        "    Search for files matching PATTERN under PATH.\n",
        NULL, 0
    },
};

const command_t *cmd_fs_commands(int *count) {
    *count = sizeof(fs_commands) / sizeof(fs_commands[0]);
    return fs_commands;
}
