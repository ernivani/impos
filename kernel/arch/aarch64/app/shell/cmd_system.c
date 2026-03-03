/* cmd_system.c — aarch64 (serial console, no GUI) */
#include <kernel/shell_cmd.h>
#include <kernel/shell.h>
#include <kernel/sh_parse.h>
#include <kernel/fs.h>
#include <kernel/tty.h>
#include <kernel/config.h>
#include <kernel/vi.h>
#include <kernel/quota.h>
#include <kernel/env.h>
#include <kernel/hostname.h>
#include <kernel/acpi.h>
#include <kernel/test.h>
#include <kernel/task.h>
#include <kernel/sched.h>
#include <kernel/rtc.h>
#include <kernel/user.h>
#include <kernel/signal.h>
#include <kernel/pmm.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

/* Aliases so job-control functions compile identically to shell.c */
#define job_table      (shell_get_job_table())
#define job_update_all shell_job_update_all

#define MAX_ARGS 64

static void cmd_clear(int argc, char* argv[]) {
    (void)argc; (void)argv;
    terminal_clear();
}

static void cmd_history(int argc, char* argv[]) {
    (void)argc; (void)argv;
    int n = shell_history_count();
    for (int i = 0; i < n; i++) {
        const char *entry = shell_history_entry(i);
        if (entry != NULL)
            printf("  %d  %s\n", i + 1, entry);
    }
}

static void cmd_pwd(int argc, char* argv[]) {
    (void)argc; (void)argv;
    printf("%s\n", fs_get_cwd());
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
    (void)argc; (void)argv;
    config_save_history();
    config_save();
    fs_sync();
}

static void cmd_exit(int argc, char* argv[]) {
    (void)argc; (void)argv;
    config_save_history();
    config_save();
    fs_sync();
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
        datetime_t dt;
        config_get_datetime(&dt);
        system_config_t* cfg = config_get();

        printf("      Local time: %d-", dt.year);
        if (dt.month < 10) { putchar('0'); } printf("%d-", dt.month);
        if (dt.day < 10) { putchar('0'); } printf("%d ", dt.day);
        if (dt.hour < 10) { putchar('0'); } printf("%d:", dt.hour);
        if (dt.minute < 10) { putchar('0'); } printf("%d:", dt.minute);
        if (dt.second < 10) { putchar('0'); } printf("%d\n", dt.second);

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
        if (argc < 3) { printf("Usage: timedatectl set-time HH:MM:SS\n"); return; }
        int hour = 0, minute = 0, second = 0;
        const char* p = argv[2];
        while (*p >= '0' && *p <= '9') hour = hour * 10 + (*p++ - '0');
        if (*p != ':') { printf("Invalid time format. Use HH:MM:SS\n"); return; }
        p++;
        while (*p >= '0' && *p <= '9') minute = minute * 10 + (*p++ - '0');
        if (*p != ':') { printf("Invalid time format. Use HH:MM:SS\n"); return; }
        p++;
        while (*p >= '0' && *p <= '9') second = second * 10 + (*p++ - '0');
        if (hour > 23 || minute > 59 || second > 59) { printf("Invalid time values\n"); return; }

        datetime_t dt;
        config_get_datetime(&dt);
        dt.hour = hour; dt.minute = minute; dt.second = second;
        config_set_datetime(&dt);
        printf("Time set to ");
        if (hour < 10) { putchar('0'); } printf("%d:", hour);
        if (minute < 10) { putchar('0'); } printf("%d:", minute);
        if (second < 10) { putchar('0'); } printf("%d\n", second);

    } else if (strcmp(argv[1], "set-date") == 0) {
        if (argc < 3) { printf("Usage: timedatectl set-date YYYY-MM-DD\n"); return; }
        int year = 0, month = 0, day = 0;
        const char* p = argv[2];
        while (*p >= '0' && *p <= '9') year = year * 10 + (*p++ - '0');
        if (*p != '-') { printf("Invalid date format. Use YYYY-MM-DD\n"); return; }
        p++;
        while (*p >= '0' && *p <= '9') month = month * 10 + (*p++ - '0');
        if (*p != '-') { printf("Invalid date format. Use YYYY-MM-DD\n"); return; }
        p++;
        while (*p >= '0' && *p <= '9') day = day * 10 + (*p++ - '0');
        if (year < 1970 || year > 9999 || month < 1 || month > 12 || day < 1 || day > 31) {
            printf("Invalid date values\n"); return;
        }

        datetime_t dt;
        config_get_datetime(&dt);
        dt.year = year; dt.month = month; dt.day = day;
        config_set_datetime(&dt);
        printf("Date set to %d-", year);
        if (month < 10) { putchar('0'); } printf("%d-", month);
        if (day < 10) { putchar('0'); } printf("%d\n", day);

    } else if (strcmp(argv[1], "set-timezone") == 0) {
        if (argc < 3) { printf("Usage: timedatectl set-timezone TIMEZONE\n"); return; }
        config_set_timezone(argv[2]);
        printf("Timezone set to %s\n", argv[2]);

    } else if (strcmp(argv[1], "list-timezones") == 0) {
        printf("Available timezones:\n");
        printf("  UTC\n  Europe/Paris\n  Europe/London\n  Europe/Berlin\n");
        printf("  America/New_York\n  America/Los_Angeles\n  America/Chicago\n");
        printf("  Asia/Tokyo\n  Asia/Shanghai\n  Australia/Sydney\n");
    } else {
        printf("Unknown command '%s'\n", argv[1]);
        printf("Use 'man timedatectl' for help\n");
    }
}

static void cmd_export(int argc, char* argv[]) {
    if (argc < 2) { printf("Usage: export VAR=value\n"); return; }
    char* equals = strchr(argv[1], '=');
    if (!equals) { printf("Invalid format. Use: export VAR=value\n"); return; }
    *equals = '\0';
    const char* name = argv[1];
    const char* value = equals + 1;
    if (env_set(name, value) == 0)
        printf("%s=%s\n", name, value);
    else
        printf("Failed to set variable\n");
}

static void cmd_env(int argc, char* argv[]) {
    (void)argc; (void)argv;
    env_list();
}

extern void test_crypto(void);
extern void test_tls(void);

static void cmd_test(int argc, char* argv[]) {
    if (argc >= 2) {
        if (strcmp(argv[1], "crypto") == 0) { test_crypto(); return; }
        if (strcmp(argv[1], "tls") == 0) { test_tls(); return; }
        printf("Unknown test suite: %s\n", argv[1]);
        printf("Available: crypto, tls (or no args for all)\n");
        return;
    }
    test_run_all();
}

static void cmd_logout(int argc, char* argv[]) {
    (void)argc; (void)argv;
    config_save_history();
    config_save();
    fs_sync();
    printf("Logging out...\n");
    exit(0);
}

static void cmd_quota(int argc, char* argv[]) {
    if (argc >= 5 && strcmp(argv[1], "-s") == 0) {
        uint16_t uid = atoi(argv[2]);
        uint16_t max_inodes = atoi(argv[3]);
        uint16_t max_blocks = atoi(argv[4]);
        if (quota_set(uid, max_inodes, max_blocks) == 0)
            printf("Quota set for uid %d: max_inodes=%d max_blocks=%d\n",
                   uid, max_inodes, max_blocks);
        else
            printf("quota: failed to set quota\n");
        return;
    }
    if (argc >= 3 && strcmp(argv[1], "-u") == 0) {
        uint16_t uid = atoi(argv[2]);
        quota_entry_t* q = quota_get(uid);
        if (q) {
            printf("Quota for uid %d:\n", uid);
            printf("  Inodes: %d / %d\n", q->used_inodes, q->max_inodes ? q->max_inodes : 0);
            printf("  Blocks: %d / %d\n", q->used_blocks, q->max_blocks ? q->max_blocks : 0);
        } else {
            printf("No quota set for uid %d\n", uid);
        }
        return;
    }
    uint16_t uid = user_get_current_uid();
    const char* uname = user_get_current();
    if (!uname) { printf("No current user\n"); return; }
    quota_entry_t* q = quota_get(uid);
    if (q) {
        printf("Quota for %s (uid %d):\n", uname, uid);
        printf("  Inodes: %d / %d\n", q->used_inodes, q->max_inodes ? q->max_inodes : 0);
        printf("  Blocks: %d / %d\n", q->used_blocks, q->max_blocks ? q->max_blocks : 0);
    } else {
        printf("No quota set for %s\n", uname);
    }
}

static void cmd_test_expr(int argc, char* argv[]) {
    int end = argc;
    if (strcmp(argv[0], "[") == 0) {
        if (argc > 1 && strcmp(argv[argc - 1], "]") == 0)
            end = argc - 1;
    }
    if (end <= 1) { sh_set_exit_code(1); return; }

    if (end == 3) {
        const char *op = argv[1], *arg = argv[2];
        if (strcmp(op, "-f") == 0) {
            uint32_t parent; char name[28];
            int ino = fs_resolve_path(arg, &parent, name);
            if (ino >= 0) { inode_t node; fs_read_inode(ino, &node); sh_set_exit_code(node.type == 1 ? 0 : 1); }
            else sh_set_exit_code(1);
            return;
        }
        if (strcmp(op, "-d") == 0) {
            uint32_t parent; char name[28];
            int ino = fs_resolve_path(arg, &parent, name);
            if (ino >= 0) { inode_t node; fs_read_inode(ino, &node); sh_set_exit_code(node.type == 2 ? 0 : 1); }
            else sh_set_exit_code(1);
            return;
        }
        if (strcmp(op, "-e") == 0) {
            uint32_t parent; char name[28];
            int ino = fs_resolve_path(arg, &parent, name);
            sh_set_exit_code(ino >= 0 ? 0 : 1);
            return;
        }
        if (strcmp(op, "-s") == 0) {
            uint32_t parent; char name[28];
            int ino = fs_resolve_path(arg, &parent, name);
            if (ino >= 0) { inode_t node; fs_read_inode(ino, &node); sh_set_exit_code(node.size > 0 ? 0 : 1); }
            else sh_set_exit_code(1);
            return;
        }
        if (strcmp(op, "-z") == 0) { sh_set_exit_code(arg[0] == '\0' ? 0 : 1); return; }
        if (strcmp(op, "-n") == 0) { sh_set_exit_code(arg[0] != '\0' ? 0 : 1); return; }
        if (strcmp(op, "!") == 0)  { sh_set_exit_code(arg[0] == '\0' ? 0 : 1); return; }
    }

    if (end == 4) {
        const char *left = argv[1], *op = argv[2], *right = argv[3];
        if (strcmp(op, "=") == 0 || strcmp(op, "==") == 0) { sh_set_exit_code(strcmp(left, right) == 0 ? 0 : 1); return; }
        if (strcmp(op, "!=") == 0) { sh_set_exit_code(strcmp(left, right) != 0 ? 0 : 1); return; }
        if (strcmp(op, "-eq") == 0) { sh_set_exit_code(atoi(left) == atoi(right) ? 0 : 1); return; }
        if (strcmp(op, "-ne") == 0) { sh_set_exit_code(atoi(left) != atoi(right) ? 0 : 1); return; }
        if (strcmp(op, "-lt") == 0) { sh_set_exit_code(atoi(left) < atoi(right) ? 0 : 1); return; }
        if (strcmp(op, "-le") == 0) { sh_set_exit_code(atoi(left) <= atoi(right) ? 0 : 1); return; }
        if (strcmp(op, "-gt") == 0) { sh_set_exit_code(atoi(left) > atoi(right) ? 0 : 1); return; }
        if (strcmp(op, "-ge") == 0) { sh_set_exit_code(atoi(left) >= atoi(right) ? 0 : 1); return; }
    }

    if (end >= 3 && strcmp(argv[1], "!") == 0) {
        char *new_argv[MAX_ARGS];
        new_argv[0] = argv[0];
        for (int i = 2; i < end; i++) new_argv[i - 1] = argv[i];
        cmd_test_expr(end - 1, new_argv);
        sh_set_exit_code(sh_get_exit_code() == 0 ? 1 : 0);
        return;
    }

    if (end == 2) { sh_set_exit_code(argv[1][0] != '\0' ? 0 : 1); return; }
    sh_set_exit_code(1);
}

static void cmd_true(int argc, char* argv[])  { (void)argc; (void)argv; sh_set_exit_code(0); }
static void cmd_false(int argc, char* argv[]) { (void)argc; (void)argv; sh_set_exit_code(1); }

/* ── Job control commands ── */

static void cmd_jobs(int argc, char* argv[]) {
    (void)argc; (void)argv;
    job_update_all();
    int found = 0;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (job_table[i].state == JOB_NONE) continue;
        const char *status = "Done";
        if (job_table[i].state == JOB_RUNNING) status = "Running";
        else if (job_table[i].state == JOB_STOPPED) status = "Stopped";
        printf("[%d] %s\t%s\n", i + 1, status, job_table[i].command);
        found = 1;
    }
    if (!found) printf("No jobs.\n");
    sh_set_exit_code(0);
}

static void cmd_fg(int argc, char* argv[]) {
    int job_num = -1;
    if (argc >= 2) {
        const char *arg = argv[1];
        if (arg[0] == '%') arg++;
        job_num = atoi(arg);
    }
    if (job_num <= 0 || job_num > MAX_JOBS) {
        for (int i = MAX_JOBS - 1; i >= 0; i--) {
            if (job_table[i].state == JOB_RUNNING || job_table[i].state == JOB_STOPPED) {
                job_num = i + 1; break;
            }
        }
    }
    if (job_num <= 0 || job_num > MAX_JOBS) { printf("fg: no current job\n"); sh_set_exit_code(1); return; }
    int idx = job_num - 1;
    if (job_table[idx].state == JOB_NONE || job_table[idx].state == JOB_DONE) {
        printf("fg: %%%d: no such job\n", job_num); sh_set_exit_code(1); return;
    }
    printf("%s\n", job_table[idx].command);
    if (job_table[idx].state == JOB_STOPPED) {
        sig_send(job_table[idx].tid, SIGCONT);
        job_table[idx].state = JOB_RUNNING;
    }
    shell_fg_app_t *app = shell_get_suspended_app(idx);
    if (app) {
        shell_register_fg_app(app);
        shell_clear_suspended_app(idx);
        job_table[idx].state = JOB_DONE;
        sh_set_exit_code(0);
        return;
    }
    task_info_t *t = task_get(job_table[idx].tid);
    while (t && t->active && t->state != TASK_STATE_ZOMBIE &&
           t->state != TASK_STATE_STOPPED)
        task_yield();
    if (t && t->state == TASK_STATE_STOPPED) {
        job_table[idx].state = JOB_STOPPED;
        printf("\n[%d] Stopped\t%s\n", job_num, job_table[idx].command);
    } else {
        job_table[idx].state = JOB_DONE;
    }
    sh_set_exit_code(0);
}

static void cmd_bg(int argc, char* argv[]) {
    int job_num = -1;
    if (argc >= 2) {
        const char *arg = argv[1];
        if (arg[0] == '%') arg++;
        job_num = atoi(arg);
    }
    if (job_num <= 0 || job_num > MAX_JOBS) {
        for (int i = MAX_JOBS - 1; i >= 0; i--) {
            if (job_table[i].state == JOB_STOPPED) { job_num = i + 1; break; }
        }
    }
    if (job_num <= 0 || job_num > MAX_JOBS) { printf("bg: no current job\n"); sh_set_exit_code(1); return; }
    int idx = job_num - 1;
    if (job_table[idx].state != JOB_STOPPED) {
        printf("bg: %%%d: not stopped\n", job_num); sh_set_exit_code(1); return;
    }
    sig_send(job_table[idx].tid, SIGCONT);
    job_table[idx].state = JOB_RUNNING;
    printf("[%d] %s &\n", job_num, job_table[idx].command);
    sh_set_exit_code(0);
}

static void cmd_sleep(int argc, char* argv[]) {
    if (argc < 2) { printf("Usage: sleep SECONDS\n"); sh_set_exit_code(1); return; }
    int secs = atoi(argv[1]);
    if (secs <= 0) { sh_set_exit_code(0); return; }
    extern void pit_sleep_ms(uint32_t ms);
    pit_sleep_ms(secs * 1000);
    sh_set_exit_code(0);
}

static const command_t system_commands[] = {
    { "clear", cmd_clear, "Clear the terminal screen",
      "clear: clear\n    Clear the terminal screen and move cursor to top.\n",
      "NAME\n    clear - clear the terminal screen\n\nSYNOPSIS\n    clear\n\nDESCRIPTION\n    Clears the terminal and resets cursor to row 0, column 0.\n", 0 },
    { "pwd", cmd_pwd, "Print the current working directory",
      "pwd: pwd\n    Print the full pathname of the current directory.\n",
      "NAME\n    pwd - print name of current/working directory\n\nSYNOPSIS\n    pwd\n", 0 },
    { "history", cmd_history, "Display command history",
      "history: history\n    List previously entered commands.\n",
      "NAME\n    history - display command history\n\nSYNOPSIS\n    history\n", 0 },
    { "vi", cmd_vi, "Edit a file with the vi text editor",
      "vi: vi FILE\n    Open FILE in the vi text editor.\n",
      "NAME\n    vi - screen-oriented text editor\n\nSYNOPSIS\n    vi FILE\n", 0 },
    { "setlayout", cmd_setlayout, "Set keyboard layout (fr/us)",
      "setlayout: setlayout LAYOUT\n    Set the keyboard layout. LAYOUT is 'fr' or 'us'.\n",
      "NAME\n    setlayout - change keyboard layout\n\nSYNOPSIS\n    setlayout [fr|us]\n", 0 },
    { "sync", cmd_sync, "Synchronize filesystem to disk",
      "sync: sync\n    Write all cached filesystem data to disk.\n",
      "NAME\n    sync - synchronize cached writes to persistent storage\n\nSYNOPSIS\n    sync\n", 0 },
    { "exit", cmd_exit, "Exit the shell and halt the CPU",
      "exit: exit [STATUS]\n    Exit the shell and halt the CPU.\n",
      "NAME\n    exit - cause normal process termination\n\nSYNOPSIS\n    exit [STATUS]\n", 0 },
    { "shutdown", cmd_shutdown, "Power off the machine",
      "shutdown: shutdown\n    Power off the machine via PSCI.\n",
      "NAME\n    shutdown - power off the machine\n\nSYNOPSIS\n    shutdown\n", CMD_FLAG_ROOT },
    { "timedatectl", cmd_timedatectl, "Control system time and date settings",
      "timedatectl: timedatectl [COMMAND]\n    Control and query system time and date settings.\n",
      "NAME\n    timedatectl - control system time and date\n\nSYNOPSIS\n    timedatectl [COMMAND] [ARGS...]\n", 0 },
    { "export", cmd_export, "Set environment variable",
      "export: export VAR=value\n    Set an environment variable.\n",
      "NAME\n    export - set environment variable\n\nSYNOPSIS\n    export VAR=value\n", 0 },
    { "env", cmd_env, "List environment variables",
      "env: env\n    Display all environment variables.\n",
      "NAME\n    env - list environment variables\n\nSYNOPSIS\n    env\n", 0 },
    { "regtest", cmd_test, "Run regression tests",
      "regtest: regtest [crypto|tls]\n    Run all or specific test suites.\n",
      "NAME\n    regtest - run regression tests\n\nSYNOPSIS\n    regtest [SUITE]\n", CMD_FLAG_ROOT },
    { "logout", cmd_logout, "Log out and return to login prompt",
      "logout: logout\n    Log out of the current session.\n",
      "NAME\n    logout - log out of the shell\n\nSYNOPSIS\n    logout\n", 0 },
    { "quota", cmd_quota, "View or set filesystem quotas",
      "quota: quota [-u USER] [-s USER INODES BLOCKS]\n    View or set per-user filesystem quotas.\n",
      "NAME\n    quota - manage filesystem quotas\n\nSYNOPSIS\n    quota [-u USER] [-s USER INODES BLOCKS]\n", CMD_FLAG_ROOT },
    /* No 'display' command on aarch64 — no framebuffer */
    { "test", cmd_test_expr, "Evaluate conditional expressions",
      "test: test EXPRESSION\n    Evaluate EXPRESSION and return 0 (true) or 1 (false).\n", NULL, 0 },
    { "[", cmd_test_expr, "Evaluate conditional expressions (alias for test)",
      "[: [ EXPRESSION ]\n    Same as 'test EXPRESSION'. The closing ] is required.\n", NULL, 0 },
    { "true", cmd_true, "Return success exit code", "true: true\n    Always returns 0.\n", NULL, 0 },
    { "false", cmd_false, "Return failure exit code", "false: false\n    Always returns 1.\n", NULL, 0 },
    { "jobs", cmd_jobs, "List background jobs", "jobs: jobs\n    List all background and stopped jobs.\n", NULL, 0 },
    { "fg", cmd_fg, "Bring job to foreground", "fg: fg [%N]\n    Bring job N to the foreground.\n", NULL, 0 },
    { "bg", cmd_bg, "Resume job in background", "bg: bg [%N]\n    Resume stopped job N in the background.\n", NULL, 0 },
    { "sleep", cmd_sleep, "Pause for specified seconds", "sleep: sleep SECONDS\n    Pause execution for SECONDS seconds.\n", NULL, 0 },
};

const command_t *cmd_system_commands(int *count) {
    *count = sizeof(system_commands) / sizeof(system_commands[0]);
    return system_commands;
}
