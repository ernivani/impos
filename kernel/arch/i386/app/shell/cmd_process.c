#include <kernel/shell_cmd.h>
#include <kernel/shell.h>
#include <kernel/sh_parse.h>
#include <kernel/task.h>
#include <kernel/sched.h>
#include <kernel/signal.h>
#include <kernel/shm.h>
#include <kernel/gfx.h>
#include <kernel/desktop.h>
#include <kernel/wm.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/idt.h>
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

static const command_t process_commands[] = {
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
};

const command_t *cmd_process_commands(int *count) {
    *count = sizeof(process_commands) / sizeof(process_commands[0]);
    return process_commands;
}
