/* cmd_process.c — aarch64 (serial console, simplified top) */
#include <kernel/shell_cmd.h>
#include <kernel/shell.h>
#include <kernel/sh_parse.h>
#include <kernel/task.h>
#include <kernel/sched.h>
#include <kernel/signal.h>
#include <kernel/shm.h>
#include <kernel/pmm.h>
#include <kernel/fs.h>
#include <kernel/tty.h>
#include <kernel/config.h>
#include <kernel/net.h>
#include <kernel/user.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

static void cmd_kill(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: kill [-9|-INT|-TERM|-KILL|-USR1|-USR2|-PIPE] PID\n");
        return;
    }

    int pid;
    int signum = SIGTERM;

    if (argv[1][0] == '-') {
        if (argc < 3) { printf("Usage: kill [-9|-INT|-TERM|-KILL|-USR1|-USR2|-PIPE] PID\n"); return; }
        const char *s = argv[1] + 1;
        if (strcmp(s, "9") == 0 || strcmp(s, "KILL") == 0) signum = SIGKILL;
        else if (strcmp(s, "INT") == 0)  signum = SIGINT;
        else if (strcmp(s, "TERM") == 0) signum = SIGTERM;
        else if (strcmp(s, "USR1") == 0) signum = SIGUSR1;
        else if (strcmp(s, "USR2") == 0) signum = SIGUSR2;
        else if (strcmp(s, "PIPE") == 0) signum = SIGPIPE;
        else { printf("kill: unknown signal '%s'\n", s); return; }
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

/* ── Simplified top for serial console ── */

static int top_tid = -1;

static void top_render(void) {
    task_set_current(top_tid);
    terminal_clear();

    datetime_t dt;
    config_get_datetime(&dt);
    uint32_t up_secs = pit_get_ticks() / 120;
    printf("top - %02d:%02d:%02d up %d:%02d:%02d, 1 user\n\n",
           (int)dt.hour, (int)dt.minute, (int)dt.second,
           (int)(up_secs / 3600), (int)((up_secs % 3600) / 60), (int)(up_secs % 60));

    /* CPU usage */
    task_info_t *idle_t = task_get(TASK_IDLE);
    int idle_x10 = 0;
    if (idle_t && idle_t->sample_total > 0)
        idle_x10 = (int)(idle_t->prev_ticks * 1000 / idle_t->sample_total);
    int cpu_pct = (1000 - idle_x10) / 10;
    if (cpu_pct < 0) cpu_pct = 0;
    printf("CPU: %d%%  idle: %d.%d%%\n", cpu_pct, idle_x10 / 10, idle_x10 % 10);

    /* Memory */
    size_t h_used = heap_used();
    size_t h_total = heap_total();
    printf("Mem: %dKB used / %dKB total\n",
           (int)(h_used / 1024), (int)(h_total / 1024));

    /* Disk */
    int used_inodes = 0, used_blocks = 0;
    for (int i = 0; i < NUM_INODES; i++) {
        inode_t tmp;
        if (fs_read_inode(i, &tmp) == 0 && tmp.type != INODE_FREE) {
            used_inodes++;
            used_blocks += tmp.num_blocks;
        }
    }
    printf("Disk: %d/%d inodes  %d/%d blocks\n",
           used_inodes, NUM_INODES, used_blocks, NUM_BLOCKS);

    /* Net */
    uint32_t tx_p = 0, tx_b = 0, rx_p = 0, rx_b = 0;
    net_get_stats(&tx_p, &tx_b, &rx_p, &rx_b);
    printf("Net: TX %d pkts  RX %d pkts\n\n", (int)tx_p, (int)rx_p);

    /* Task counts */
    int n_total = 0, n_running = 0, n_sleeping = 0;
    for (int i = 0; i < TASK_MAX; i++) {
        task_info_t *t = task_get(i);
        if (!t) continue;
        n_total++;
        if (i == TASK_IDLE) continue;
        int pct = t->sample_total > 0 ? (int)(t->prev_ticks * 100 / t->sample_total) : 0;
        if (pct > 0) n_running++;
        else n_sleeping++;
    }
    printf("Tasks: %d total, %d running, %d sleeping\n\n", n_total, n_running, n_sleeping);

    /* Process table */
    const char *cur_user = user_get_current();
    if (!cur_user) cur_user = "root";
    printf("  %5s %-8s S %%CPU    RES     TIME+ COMMAND\n", "PID", "USER");

    /* Sort by CPU desc */
    int indices[TASK_MAX];
    int count = 0;
    for (int i = 0; i < TASK_MAX; i++) {
        if (task_get(i)) indices[count++] = i;
    }
    for (int i = 1; i < count; i++) {
        int key = indices[i];
        task_info_t *kt = task_get(key);
        int kpct = kt->sample_total > 0 ? (int)(kt->prev_ticks * 1000 / kt->sample_total) : 0;
        int j = i - 1;
        while (j >= 0) {
            task_info_t *jt = task_get(indices[j]);
            int jpct = jt->sample_total > 0 ? (int)(jt->prev_ticks * 1000 / jt->sample_total) : 0;
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
        char state = (i == TASK_IDLE) ? 'I' : (task_cpu_x10 > 0 ? 'R' : 'S');

        uint32_t tticks = t->total_ticks;
        uint32_t tsecs = tticks / 120;
        uint32_t tcs = (tticks % 120) * 100 / 120;
        uint32_t tmins = tsecs / 60;
        uint32_t ts = tsecs % 60;
        const char *uname = t->killable ? cur_user : "root";

        printf("  %5d %-8s %c %2d.%d %5dK  %4d:%02d.%02d %s\n",
               task_get_pid(i), uname, state,
               task_cpu_x10 / 10, task_cpu_x10 % 10,
               t->mem_kb,
               (int)tmins, (int)ts, (int)tcs,
               t->name);
    }

    printf("\nPress 'q' to quit, refreshes every 1s\n");
}

static void top_on_key(char c) {
    if (c == 'q' || c == 'Q') {
        if (top_tid >= 0) task_unregister(top_tid);
        top_tid = -1;
        shell_unregister_fg_app();
        terminal_clear();
        shell_draw_prompt();
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
    top_render();
    shell_register_fg_app(&top_fg_app);
}

/* ── Thread spawning ── */

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

/* Ring 3 user-mode counter — aarch64 SVC syscalls */
static void user_thread_counter(void) {
    int pid;
    register uint64_t x0 __asm__("x0");
    register uint64_t x8 __asm__("x8") = 3;  /* SYS_GETPID */
    __asm__ volatile ("svc #0" : "=r"(x0) : "r"(x8) : "memory");
    pid = (int)x0;

    for (int i = 0; ; i++) {
        printf("[user %d] count = %d\n", pid, i);
        register uint64_t r0 __asm__("x0") = 1000;  /* 1000ms */
        register uint64_t r8 __asm__("x8") = 2;     /* SYS_SLEEP */
        __asm__ volatile ("svc #0" : "+r"(r0) : "r"(r8) : "memory");
    }
}

static void cmd_spawn(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: spawn [counter|hog|user-counter]\n");
        return;
    }

    if (strcmp(argv[1], "user-counter") == 0) {
        int tid = task_create_user_thread("user-counter", user_thread_counter, 1);
        if (tid < 0) { printf("spawn: failed to create user thread\n"); return; }
        printf("[User Thread %d] user-counter started (PID %d, ring 3)\n",
               tid, task_get_pid(tid));
        return;
    }

    void (*entry)(void) = 0;
    const char *name = argv[1];

    if (strcmp(argv[1], "counter") == 0) entry = thread_counter;
    else if (strcmp(argv[1], "hog") == 0) entry = thread_hog;
    else { printf("spawn: unknown thread type '%s'\n  Available: counter, hog, user-counter\n", argv[1]); return; }

    int tid = task_create_thread(name, entry, 1);
    if (tid < 0) { printf("spawn: failed to create thread\n"); return; }
    printf("[Thread %d] %s started (PID %d)\n", tid, name, task_get_pid(tid));
}

/* ═══ shm ═══════════════════════════════════════════════════ */

static void cmd_shm(int argc, char* argv[]) {
    if (argc < 2) { printf("Usage: shm [list|create NAME SIZE]\n"); return; }

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
        if (!found) printf("(no shared memory regions)\n");

    } else if (strcmp(argv[1], "create") == 0) {
        if (argc < 4) { printf("Usage: shm create NAME SIZE\n"); return; }
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

static const command_t process_commands[] = {
    { "top", cmd_top, "Display live system information",
      "top: top\n    Display live-updating system stats.\n    Press 'q' to quit.\n",
      "NAME\n    top - display live system information\n\nSYNOPSIS\n    top\n", 0 },
    { "kill", cmd_kill, "Send a signal to a process",
      "kill: kill [-9|-INT|-TERM|-KILL|-USR1|-USR2|-PIPE] PID\n    Send a signal to the process with the given PID.\n",
      "NAME\n    kill - send a signal to a process\n\nSYNOPSIS\n    kill [-SIGNAL] PID\n", 0 },
    { "spawn", cmd_spawn, "Spawn a background thread",
      "spawn: spawn [counter|hog|user-counter]\n    Spawn a background thread for testing.\n",
      "NAME\n    spawn - spawn a background thread\n\nSYNOPSIS\n    spawn TYPE\n", CMD_FLAG_ROOT },
    { "shm", cmd_shm, "Manage shared memory regions",
      "shm: shm [list|create NAME SIZE]\n    Manage shared memory regions.\n",
      "NAME\n    shm - manage shared memory regions\n\nSYNOPSIS\n    shm COMMAND [ARGS]\n", CMD_FLAG_ROOT },
};

const command_t *cmd_process_commands(int *count) {
    *count = sizeof(process_commands) / sizeof(process_commands[0]);
    return process_commands;
}
