/* cmd_exec.c — aarch64 (no PE loader, no USB, no DOOM) */
#include <kernel/shell_cmd.h>
#include <kernel/sh_parse.h>
#include <kernel/elf_loader.h>
#include <kernel/pe_loader.h>
#include <kernel/task.h>
#include <kernel/sched.h>
#include <kernel/pci.h>
#include <kernel/fs.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

static void cmd_lspci(int argc, char* argv[]) {
    (void)argc; (void)argv;
    pci_scan_bus();
}

static void cmd_run(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: run <file>\n");
        return;
    }

    /* Try ELF */
    int ret = elf_run_argv(argv[1], argc - 1, (const char **)&argv[1]);
    if (ret >= 0) {
        task_info_t *t = task_get(ret);
        printf("Started ELF process '%s' (PID %d)\n", argv[1],
               t ? t->pid : ret);
        while (t && t->active && t->state != TASK_STATE_ZOMBIE)
            task_yield();
        return;
    }

    printf("Failed to run '%s' (error %d)\n", argv[1], ret);
}

static const command_t exec_commands[] = {
    { "run", cmd_run, "Run an ELF executable",
      "run: run FILE\n    Load and execute an ELF binary.\n",
      "NAME\n    run - execute an ELF binary\n\nSYNOPSIS\n    run FILE\n", 0 },
    { "lspci", cmd_lspci, "List all PCI devices",
      "lspci: lspci\n    List all PCI devices on the system.\n",
      "NAME\n    lspci - list PCI devices\n\nSYNOPSIS\n    lspci\n", 0 },
};

const command_t *cmd_exec_commands(int *count) {
    *count = sizeof(exec_commands) / sizeof(exec_commands[0]);
    return exec_commands;
}
