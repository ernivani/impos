#include <kernel/shell_cmd.h>
#include <kernel/sh_parse.h>
#include <kernel/fs.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

static void cmd_echo(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) printf(" ");
        printf("%s", argv[i]);
    }
    printf("\n");
}

static void cmd_grep(int argc, char* argv[]) {
    int opt_i = 0, opt_n = 0, opt_v = 0, opt_c = 0;
    const char *pattern = NULL;
    const char *filename = NULL;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (int j = 1; argv[i][j]; j++) {
                if (argv[i][j] == 'i') opt_i = 1;
                else if (argv[i][j] == 'n') opt_n = 1;
                else if (argv[i][j] == 'v') opt_v = 1;
                else if (argv[i][j] == 'c') opt_c = 1;
            }
        } else if (!pattern) {
            pattern = argv[i];
        } else if (!filename) {
            filename = argv[i];
        }
    }
    if (!pattern) {
        printf("Usage: grep [-invc] PATTERN [FILE]\n");
        sh_set_exit_code(2);
        return;
    }

    int buf_len = 0;
    char *buf = shell_read_input(filename, &buf_len);
    if (!buf) {
        if (filename) sh_set_exit_code(2);
        else sh_set_exit_code(1);
        return;
    }

    int match_count = 0, line_num = 0;
    int plen = strlen(pattern);
    const char *p = buf;
    const char *end = buf + buf_len;

    while (p < end) {
        const char *nl = p;
        while (nl < end && *nl != '\n') nl++;
        int line_len = nl - p;
        line_num++;

        /* Check for match */
        int matched = 0;
        for (int i = 0; i <= line_len - plen; i++) {
            int m = 1;
            for (int j = 0; j < plen; j++) {
                char a = p[i + j], b = pattern[j];
                if (opt_i) {
                    if (a >= 'A' && a <= 'Z') a += 32;
                    if (b >= 'A' && b <= 'Z') b += 32;
                }
                if (a != b) { m = 0; break; }
            }
            if (m) { matched = 1; break; }
        }

        if (opt_v) matched = !matched;

        if (matched) {
            match_count++;
            if (!opt_c) {
                if (opt_n) printf("%d:", line_num);
                for (int i = 0; i < line_len; i++)
                    putchar(p[i]);
                putchar('\n');
            }
        }

        p = (nl < end) ? nl + 1 : end;
    }

    if (opt_c) printf("%d\n", match_count);
    free(buf);
    sh_set_exit_code(match_count > 0 ? 0 : 1);
}

static void cmd_wc(int argc, char* argv[]) {
    const char *filename = NULL;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') { filename = argv[i]; break; }
    }

    int buf_len = 0;
    char *buf = shell_read_input(filename, &buf_len);
    if (!buf) {
        if (filename) sh_set_exit_code(1);
        printf("  0  0  0\n");
        return;
    }

    int lines = 0, words = 0, chars = buf_len;
    int in_word = 0;
    for (int i = 0; i < buf_len; i++) {
        if (buf[i] == '\n') lines++;
        if (buf[i] == ' ' || buf[i] == '\n' || buf[i] == '\t') {
            in_word = 0;
        } else {
            if (!in_word) words++;
            in_word = 1;
        }
    }
    if (buf_len > 0 && buf[buf_len - 1] != '\n') lines++;
    printf("  %d  %d  %d", lines, words, chars);
    if (filename) printf(" %s", filename);
    printf("\n");
    free(buf);
    sh_set_exit_code(0);
}

static void cmd_head(int argc, char* argv[]) {
    int count = 10;
    const char *filename = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            count = atoi(argv[++i]);
        } else if (argv[i][0] != '-') {
            filename = argv[i];
        }
    }

    int buf_len = 0;
    char *buf = shell_read_input(filename, &buf_len);
    if (!buf) { sh_set_exit_code(1); return; }

    int lines = 0;
    for (int i = 0; i < buf_len && lines < count; i++) {
        putchar(buf[i]);
        if (buf[i] == '\n') lines++;
    }
    free(buf);
    sh_set_exit_code(0);
}

static void cmd_tail(int argc, char* argv[]) {
    int count = 10;
    const char *filename = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            count = atoi(argv[++i]);
        } else if (argv[i][0] != '-') {
            filename = argv[i];
        }
    }

    int buf_len = 0;
    char *buf = shell_read_input(filename, &buf_len);
    if (!buf) { sh_set_exit_code(1); return; }

    /* Count total lines */
    int total_lines = 0;
    for (int i = 0; i < buf_len; i++) {
        if (buf[i] == '\n') total_lines++;
    }
    if (buf_len > 0 && buf[buf_len - 1] != '\n') total_lines++;

    /* Skip lines until we're at the right position */
    int skip = total_lines - count;
    if (skip < 0) skip = 0;
    int lines = 0;
    int start = 0;
    for (int i = 0; i < buf_len && lines < skip; i++) {
        if (buf[i] == '\n') {
            lines++;
            start = i + 1;
        }
    }

    for (int i = start; i < buf_len; i++)
        putchar(buf[i]);
    if (buf_len > 0 && buf[buf_len - 1] != '\n')
        putchar('\n');
    free(buf);
    sh_set_exit_code(0);
}

static void cmd_sort(int argc, char* argv[]) {
    const char *filename = NULL;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') { filename = argv[i]; break; }
    }

    int buf_len = 0;
    char *buf = shell_read_input(filename, &buf_len);
    if (!buf) { sh_set_exit_code(1); return; }

    /* Split into lines */
    #define SORT_MAX_LINES 256
    char *lines[SORT_MAX_LINES];
    int line_count = 0;
    char *p = buf;
    while (*p && line_count < SORT_MAX_LINES) {
        lines[line_count++] = p;
        while (*p && *p != '\n') p++;
        if (*p == '\n') *p++ = '\0';
    }

    /* Insertion sort */
    for (int i = 1; i < line_count; i++) {
        char *key = lines[i];
        int j = i - 1;
        while (j >= 0 && strcmp(lines[j], key) > 0) {
            lines[j + 1] = lines[j];
            j--;
        }
        lines[j + 1] = key;
    }

    for (int i = 0; i < line_count; i++)
        printf("%s\n", lines[i]);

    free(buf);
    sh_set_exit_code(0);
}

static void cmd_uniq(int argc, char* argv[]) {
    const char *filename = NULL;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') { filename = argv[i]; break; }
    }

    int buf_len = 0;
    char *buf = shell_read_input(filename, &buf_len);
    if (!buf) { sh_set_exit_code(1); return; }

    char prev_line[256] = "";
    const char *p = buf;
    const char *end = buf + buf_len;
    while (p < end) {
        const char *nl = p;
        while (nl < end && *nl != '\n') nl++;
        int len = nl - p;
        if (len > 255) len = 255;

        char line[256];
        memcpy(line, p, len);
        line[len] = '\0';

        if (strcmp(line, prev_line) != 0) {
            printf("%s\n", line);
            strcpy(prev_line, line);
        }

        p = (nl < end) ? nl + 1 : end;
    }

    free(buf);
    sh_set_exit_code(0);
}

static void cmd_tee(int argc, char* argv[]) {
    int append = 0;
    const char *filename = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-a") == 0) append = 1;
        else filename = argv[i];
    }
    if (!filename) {
        printf("Usage: tee [-a] FILE\n");
        sh_set_exit_code(1);
        return;
    }

    /* Read from pipe input */
    if (!shell_has_pipe_input()) {
        sh_set_exit_code(0);
        return;
    }

    int buf_len = 0;
    char *buf = shell_read_input(NULL, &buf_len);
    if (!buf || buf_len == 0) {
        if (buf) free(buf);
        sh_set_exit_code(0);
        return;
    }

    /* Write to stdout */
    for (int i = 0; i < buf_len; i++)
        putchar(buf[i]);

    /* Write to file */
    fs_create_file(filename, 0);
    uint32_t parent;
    char name[28];
    int ino = fs_resolve_path(filename, &parent, name);
    if (ino >= 0) {
        uint32_t offset = 0;
        if (append) {
            inode_t node;
            fs_read_inode(ino, &node);
            offset = node.size;
        } else {
            fs_truncate_inode(ino, 0);
        }
        fs_write_at(ino, (const uint8_t *)buf, offset, buf_len);
    }

    free(buf);
    sh_set_exit_code(0);
}

static const command_t text_commands[] = {
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
        "grep", cmd_grep,
        "Search files for a pattern",
        "grep: grep [-i] [-n] [-v] [-c] PATTERN [FILE...]\n"
        "    Search for PATTERN in each FILE or stdin.\n"
        "    -i: case insensitive  -n: line numbers\n"
        "    -v: invert match      -c: count only\n",
        NULL, 0
    },
    {
        "wc", cmd_wc,
        "Count lines, words, bytes",
        "wc: wc [FILE...]\n"
        "    Print line, word, and byte counts for each FILE.\n"
        "    With no FILE, reads from pipe input.\n",
        NULL, 0
    },
    {
        "head", cmd_head,
        "Output first lines of a file",
        "head: head [-n COUNT] [FILE]\n"
        "    Print the first COUNT lines (default 10) of FILE.\n",
        NULL, 0
    },
    {
        "tail", cmd_tail,
        "Output last lines of a file",
        "tail: tail [-n COUNT] [FILE]\n"
        "    Print the last COUNT lines (default 10) of FILE.\n",
        NULL, 0
    },
    {
        "sort", cmd_sort,
        "Sort lines of text",
        "sort: sort [FILE]\n"
        "    Sort lines alphabetically from FILE or stdin.\n",
        NULL, 0
    },
    {
        "uniq", cmd_uniq,
        "Remove duplicate adjacent lines",
        "uniq: uniq [FILE]\n"
        "    Remove adjacent duplicate lines from FILE or stdin.\n",
        NULL, 0
    },
    {
        "tee", cmd_tee,
        "Read stdin, write to stdout and file",
        "tee: tee [-a] FILE\n"
        "    Copy stdin to stdout and FILE. -a: append.\n",
        NULL, 0
    },
};

const command_t *cmd_text_commands(int *count) {
    *count = sizeof(text_commands) / sizeof(text_commands[0]);
    return text_commands;
}
