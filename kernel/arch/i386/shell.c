#include <kernel/shell.h>
#include <kernel/fs.h>
#include <string.h>
#include <stdio.h>
#include <string.h>

#define MAX_COMMAND_LENGTH 1024
#define MAX_ARGS 64

static void cmd_echo(int argc, char* argv[]);
static void cmd_cat(int argc, char* argv[]);
static void cmd_ls(int argc, char* argv[]);
static void cmd_cd(int argc, char* argv[]);
static void cmd_touch(int argc, char* argv[]);

void shell_initialize(void) {
    fs_initialize();
    printf("Simple Shell v1.0\n");
}

void shell_process_command(char* command) {
    char* argv[MAX_ARGS];
    int argc = 0;
    
    // Parse command into arguments
    char* token = strtok(command, " ");
    while (token != NULL && argc < MAX_ARGS) {
        argv[argc++] = token;
        token = strtok(NULL, " ");
    }

    if (argc == 0) return;

    // Process commands
    if (strcmp(argv[0], "echo") == 0) {
        cmd_echo(argc, argv);
    } else if (strcmp(argv[0], "cat") == 0) {
        cmd_cat(argc, argv);
    } else if (strcmp(argv[0], "ls") == 0) {
        cmd_ls(argc, argv);
    } else if (strcmp(argv[0], "cd") == 0) {
        cmd_cd(argc, argv);
    } else if (strcmp(argv[0], "touch") == 0) {
        cmd_touch(argc, argv);
    } else {
        printf("Unknown command: %s\n", argv[0]);
    }
}

static void cmd_echo(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        printf("%s ", argv[i]);
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
        printf("Error: Could not read file %s\n", argv[1]);
    }
}

static void cmd_ls(int argc, char* argv[]) {
    fs_list_directory();
}

static void cmd_cd(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: cd <directory>\n");
        return;
    }
    
    if (fs_change_directory(argv[1]) != 0) {
        printf("Error: Could not change to directory %s\n", argv[1]);
    }
}

static void cmd_touch(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: touch <filename>\n");
        return;
    }
    
    if (fs_create_file(argv[1], 0) != 0) {
        printf("Error: Could not create file %s\n", argv[1]);
    }
}