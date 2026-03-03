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
#include <kernel/gfx.h>
#include <kernel/desktop.h>
#include <kernel/task.h>
#include <kernel/sched.h>
#include <kernel/rtc.h>
#include <kernel/user.h>
#include <kernel/wm.h>
#include <kernel/mouse.h>
#include <kernel/signal.h>
#include <kernel/ui_event.h>
#include <kernel/pmm.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

/* Aliases so job-control functions compile identically to shell.c */
#define job_table      (shell_get_job_table())
#define job_update_all shell_job_update_all

#define MAX_ARGS 64

/* ═══ Integer sine/cosine table (0..63 → 0..255, quarter-wave) ══ */
static const int16_t sin_tab64[65] = {
      0,  6, 13, 19, 25, 31, 37, 44, 50, 56, 62, 68, 74, 80, 86, 92,
     97,103,109,114,120,125,130,136,141,146,150,155,160,164,169,173,
    177,181,185,189,193,196,200,203,206,209,212,215,218,220,223,225,
    227,229,231,233,234,236,237,238,240,241,241,242,243,243,243,244,244
};

/* Returns sin(angle)*256 where angle is 0..1023 for full circle */
static int isin(int angle) {
    angle = angle & 1023;
    int q = angle >> 8;          /* quadrant 0-3 */
    int idx = angle & 255;       /* position in quadrant (0-255) */
    /* Map 0-255 to 0-64 table index */
    int ti = idx >> 2;           /* 0..63 */
    int val = sin_tab64[ti];
    switch (q) {
        case 0: return  val;
        case 1: return  sin_tab64[64 - ti];
        case 2: return -val;
        case 3: return -sin_tab64[64 - ti];
    }
    return 0;
}
static int icos(int angle) { return isin(angle + 256); }

/* HSV to RGB: h 0..1023, s,v 0..255 */
static uint32_t hsv_to_rgb(int h, int s, int v) {
    h = h & 1023;
    int region = h * 6 / 1024;
    int remainder = (h * 6 - region * 1024) * 255 / 1024;
    int p = v * (255 - s) / 255;
    int q = v * (255 - (s * remainder / 255)) / 255;
    int t2 = v * (255 - (s * (255 - remainder) / 255)) / 255;
    switch (region) {
        case 0:  return GFX_RGB(v, t2, p);
        case 1:  return GFX_RGB(q, v, p);
        case 2:  return GFX_RGB(p, v, t2);
        case 3:  return GFX_RGB(p, q, v);
        case 4:  return GFX_RGB(t2, p, v);
        default: return GFX_RGB(v, p, q);
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
        if (dt.month < 10) { putchar('0'); } printf("%d-", dt.month);
        if (dt.day < 10) { putchar('0'); } printf("%d ", dt.day);
        if (dt.hour < 10) { putchar('0'); } printf("%d:", dt.hour);
        if (dt.minute < 10) { putchar('0'); } printf("%d:", dt.minute);
        if (dt.second < 10) { putchar('0'); } printf("%d\n", dt.second);

        /* Universal time */
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
        if (hour < 10) { putchar('0'); } printf("%d:", hour);
        if (minute < 10) { putchar('0'); } printf("%d:", minute);
        if (second < 10) { putchar('0'); } printf("%d\n", second);

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
        if (month < 10) { putchar('0'); } printf("%d-", month);
        if (day < 10) { putchar('0'); } printf("%d\n", day);

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

/* Forward declarations for individual test suites */
extern void test_crypto(void);
extern void test_tls(void);

static void cmd_test(int argc, char* argv[]) {
    if (argc >= 2) {
        if (strcmp(argv[1], "crypto") == 0) {
            test_crypto();
            return;
        }
        if (strcmp(argv[1], "tls") == 0) {
            test_tls();
            return;
        }
        printf("Unknown test suite: %s\n", argv[1]);
        printf("Available: crypto, tls (or no args for all)\n");
        return;
    }
    test_run_all();
}

static void cmd_logout(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    config_save_history();
    config_save();
    fs_sync();
    if (gfx_is_active()) {
        shell_exit_requested = 1;
        return;
    }
    printf("Logging out...\n");
    exit(0);
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

static void cmd_display(int argc, char* argv[]) {
    (void)argc; (void)argv;

    if (!gfx_is_active()) {
        printf("Graphics mode not available\n");
        return;
    }

    int W = (int)gfx_width();
    int H = (int)gfx_height();

    keyboard_set_idle_callback(0);
    int tid = task_register("display", 1, -1);

    uint32_t frame = 0;
    uint32_t fps = 0;
    uint32_t sec_start = pit_get_ticks();
    uint32_t frames_this_sec = 0;
    uint32_t fps_min = 0, fps_max = 0;
    uint32_t fps_accum = 0, fps_samples = 0;

    while (1) {
        uint32_t now = pit_get_ticks();

        /* Calculate FPS every second (120 ticks) */
        if (now - sec_start >= 120) {
            fps = frames_this_sec * 120 / (now - sec_start);
            if (fps_samples == 0 || fps < fps_min) fps_min = fps;
            if (fps > fps_max) fps_max = fps;
            fps_accum += fps;
            fps_samples++;
            frames_this_sec = 0;
            sec_start = now;
        }

        /* Background */
        gfx_clear(GFX_RGB(18, 20, 28));

        /* Animated ring of dots — light load to show real frame rate */
        int cx = W / 2, cy = H / 2 + 60;
        for (int i = 0; i < 12; i++) {
            int angle = (int)frame * 3 + i * 85;
            int rx = cx + isin(angle) * 180 / 244;
            int ry = cy + icos(angle) * 180 / 244;
            uint32_t c = hsv_to_rgb((i * 85 + (int)frame * 4) & 1023, 200, 220);
            gfx_fill_circle(rx, ry, 12, c);
        }

        char buf[128];

        /* FPS — big centered */
        snprintf(buf, sizeof(buf), "FPS: %u", (unsigned)fps);
        gfx_draw_string_scaled(cx - gfx_string_scaled_w(buf, 5) / 2,
                               50, buf, GFX_WHITE, 5);

        /* Min / Avg / Max */
        snprintf(buf, sizeof(buf), "MIN: %u   AVG: %u   MAX: %u",
                 (unsigned)(fps_samples > 0 ? fps_min : 0),
                 (unsigned)(fps_samples > 0 ? fps_accum / fps_samples : 0),
                 (unsigned)fps_max);
        gfx_draw_string_scaled(cx - gfx_string_scaled_w(buf, 2) / 2,
                               140, buf, GFX_RGB(180, 185, 200), 2);

        /* Mouse info */
        int mx = mouse_get_x(), my = mouse_get_y();
        uint8_t mb = mouse_get_buttons();
        snprintf(buf, sizeof(buf), "Mouse: %d, %d   Buttons: %c %c %c",
                 mx, my,
                 (mb & 1) ? 'L' : '-',
                 (mb & 4) ? 'M' : '-',
                 (mb & 2) ? 'R' : '-');
        gfx_draw_string_scaled(cx - gfx_string_scaled_w(buf, 2) / 2,
                               190, buf, GFX_RGB(160, 165, 180), 2);

        /* Crosshair at mouse position (drawn to backbuffer) */
        gfx_draw_line(mx - 30, my, mx - 6, my, GFX_RGB(255, 60, 60));
        gfx_draw_line(mx + 6, my, mx + 30, my, GFX_RGB(255, 60, 60));
        gfx_draw_line(mx, my - 30, mx, my - 6, GFX_RGB(255, 60, 60));
        gfx_draw_line(mx, my + 6, mx, my + 30, GFX_RGB(255, 60, 60));
        gfx_fill_circle(mx, my, 3, GFX_RGB(255, 60, 60));

        /* Frame counter + help */
        snprintf(buf, sizeof(buf), "Frame: %u", (unsigned)frame);
        gfx_draw_string(cx - (int)strlen(buf) * FONT_W / 2, H - 60,
                         buf, GFX_RGB(100, 105, 120), GFX_RGB(18, 20, 28));
        gfx_draw_string(cx - 11 * FONT_W / 2, H - 30,
                         "Q: quit", GFX_RGB(80, 85, 100), GFX_RGB(18, 20, 28));

        gfx_flip();
        frame++;
        frames_this_sec++;

        if (tid >= 0 && task_check_killed(tid)) break;

        int key = keyboard_getchar_nb();
        if (key == 'q' || key == 'Q' || key == 27) break;

        /* No frame cap — measure true rendering throughput */
    }

    if (tid >= 0) task_unregister(tid);
    keyboard_set_idle_callback(desktop_get_idle_terminal_cb());
    terminal_clear();
    if (gfx_is_active()) wm_composite();
}

static void cmd_test_expr(int argc, char* argv[]) {
    /* If invoked as [, remove trailing ] */
    int end = argc;
    if (strcmp(argv[0], "[") == 0) {
        if (argc > 1 && strcmp(argv[argc - 1], "]") == 0)
            end = argc - 1;
    }

    /* No args → false */
    if (end <= 1) {
        sh_set_exit_code(1);
        return;
    }

    /* Unary file tests: -f, -d, -e, -s, -r, -w, -x */
    if (end == 3) {
        const char *op = argv[1];
        const char *arg = argv[2];

        if (strcmp(op, "-f") == 0) {
            uint32_t parent; char name[28];
            int ino = fs_resolve_path(arg, &parent, name);
            if (ino >= 0) {
                inode_t node; fs_read_inode(ino, &node);
                sh_set_exit_code(node.type == 1 ? 0 : 1);
            } else {
                sh_set_exit_code(1);
            }
            return;
        }
        if (strcmp(op, "-d") == 0) {
            uint32_t parent; char name[28];
            int ino = fs_resolve_path(arg, &parent, name);
            if (ino >= 0) {
                inode_t node; fs_read_inode(ino, &node);
                sh_set_exit_code(node.type == 2 ? 0 : 1);
            } else {
                sh_set_exit_code(1);
            }
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
            if (ino >= 0) {
                inode_t node; fs_read_inode(ino, &node);
                sh_set_exit_code(node.size > 0 ? 0 : 1);
            } else {
                sh_set_exit_code(1);
            }
            return;
        }
        if (strcmp(op, "-z") == 0) {
            sh_set_exit_code(arg[0] == '\0' ? 0 : 1);
            return;
        }
        if (strcmp(op, "-n") == 0) {
            sh_set_exit_code(arg[0] != '\0' ? 0 : 1);
            return;
        }
        if (strcmp(op, "!") == 0) {
            /* Unary negation with single arg */
            sh_set_exit_code(arg[0] == '\0' ? 0 : 1);
            return;
        }
    }

    /* Binary operators: =, !=, -eq, -ne, -lt, -le, -gt, -ge */
    if (end == 4) {
        const char *left = argv[1];
        const char *op = argv[2];
        const char *right = argv[3];

        if (strcmp(op, "=") == 0 || strcmp(op, "==") == 0) {
            sh_set_exit_code(strcmp(left, right) == 0 ? 0 : 1);
            return;
        }
        if (strcmp(op, "!=") == 0) {
            sh_set_exit_code(strcmp(left, right) != 0 ? 0 : 1);
            return;
        }
        if (strcmp(op, "-eq") == 0) {
            sh_set_exit_code(atoi(left) == atoi(right) ? 0 : 1);
            return;
        }
        if (strcmp(op, "-ne") == 0) {
            sh_set_exit_code(atoi(left) != atoi(right) ? 0 : 1);
            return;
        }
        if (strcmp(op, "-lt") == 0) {
            sh_set_exit_code(atoi(left) < atoi(right) ? 0 : 1);
            return;
        }
        if (strcmp(op, "-le") == 0) {
            sh_set_exit_code(atoi(left) <= atoi(right) ? 0 : 1);
            return;
        }
        if (strcmp(op, "-gt") == 0) {
            sh_set_exit_code(atoi(left) > atoi(right) ? 0 : 1);
            return;
        }
        if (strcmp(op, "-ge") == 0) {
            sh_set_exit_code(atoi(left) >= atoi(right) ? 0 : 1);
            return;
        }
    }

    /* Negation: ! expr */
    if (end >= 3 && strcmp(argv[1], "!") == 0) {
        char *new_argv[MAX_ARGS];
        new_argv[0] = argv[0];
        for (int i = 2; i < end; i++)
            new_argv[i - 1] = argv[i];
        cmd_test_expr(end - 1, new_argv);
        sh_set_exit_code(sh_get_exit_code() == 0 ? 1 : 0);
        return;
    }

    /* Single string: non-empty → true */
    if (end == 2) {
        sh_set_exit_code(argv[1][0] != '\0' ? 0 : 1);
        return;
    }

    sh_set_exit_code(1);
}

static void cmd_true(int argc, char* argv[]) {
    (void)argc; (void)argv;
    sh_set_exit_code(0);
}

static void cmd_false(int argc, char* argv[]) {
    (void)argc; (void)argv;
    sh_set_exit_code(1);
}

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
        /* Find most recent running/stopped job */
        for (int i = MAX_JOBS - 1; i >= 0; i--) {
            if (job_table[i].state == JOB_RUNNING || job_table[i].state == JOB_STOPPED) {
                job_num = i + 1;
                break;
            }
        }
    }
    if (job_num <= 0 || job_num > MAX_JOBS) {
        printf("fg: no current job\n");
        sh_set_exit_code(1);
        return;
    }
    int idx = job_num - 1;
    if (job_table[idx].state == JOB_NONE || job_table[idx].state == JOB_DONE) {
        printf("fg: %%%d: no such job\n", job_num);
        sh_set_exit_code(1);
        return;
    }
    printf("%s\n", job_table[idx].command);
    /* Send SIGCONT if stopped */
    if (job_table[idx].state == JOB_STOPPED) {
        sig_send(job_table[idx].tid, SIGCONT);
        job_table[idx].state = JOB_RUNNING;
    }
    /* Check for suspended fg app (builtin like top) — uses callback model */
    shell_fg_app_t *app = shell_get_suspended_app(idx);
    if (app) {
        shell_register_fg_app(app);
        shell_clear_suspended_app(idx);
        job_table[idx].state = JOB_DONE;
        sh_set_exit_code(0);
        return;
    }
    /* Wait for it to finish */
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
            if (job_table[i].state == JOB_STOPPED) {
                job_num = i + 1;
                break;
            }
        }
    }
    if (job_num <= 0 || job_num > MAX_JOBS) {
        printf("bg: no current job\n");
        sh_set_exit_code(1);
        return;
    }
    int idx = job_num - 1;
    if (job_table[idx].state != JOB_STOPPED) {
        printf("bg: %%%d: not stopped\n", job_num);
        sh_set_exit_code(1);
        return;
    }
    sig_send(job_table[idx].tid, SIGCONT);
    job_table[idx].state = JOB_RUNNING;
    printf("[%d] %s &\n", job_num, job_table[idx].command);
    sh_set_exit_code(0);
}

static void cmd_sleep(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: sleep SECONDS\n");
        sh_set_exit_code(1);
        return;
    }
    int secs = atoi(argv[1]);
    if (secs <= 0) {
        sh_set_exit_code(0);
        return;
    }
    extern void pit_sleep_ms(uint32_t ms);
    pit_sleep_ms(secs * 1000);
    sh_set_exit_code(0);
}

static const command_t system_commands[] = {
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
        "    the cursor position to row 0, column 0.\n",
        0
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
        "    directory by walking the .. chain up to /.\n",
        0
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
        "    Use Up/Down in the shell to recall history.\n",
        0
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
        "    :q!       Quit without saving\n",
        0
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
        "    Without arguments, prints the current layout.\n",
        0
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
        "    this command forces an immediate sync.\n",
        0
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
        "    0 indicates success, nonzero indicates failure.\n",
        0
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
        "    the CPU (same as 'exit').\n",
        CMD_FLAG_ROOT
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
        "        List common available timezones.\n",
        0
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
        "    export HOME=/home/user\n",
        0
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
        "    variables and their values.\n",
        0
    },
    {
        "regtest", cmd_test,
        "Run regression tests",
        "regtest: regtest [crypto|tls]\n"
        "    Run all or specific test suites.\n",
        "NAME\n"
        "    regtest - run regression tests\n\n"
        "SYNOPSIS\n"
        "    regtest [SUITE]\n\n"
        "DESCRIPTION\n"
        "    Run all built-in test suites and print results.\n"
        "    Optional SUITE: crypto, tls\n",
        CMD_FLAG_ROOT
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
        "    The current user session is ended.\n",
        0
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
        "    INODES and BLOCKS are maximum counts (0 = unlimited).\n",
        CMD_FLAG_ROOT
    },
    {
        "display", cmd_display,
        "Show real-time FPS and input monitor",
        "display: display\n"
        "    Show a live FPS counter, mouse coordinates, and input state.\n"
        "    Press 'q' to quit.\n",
        "NAME\n"
        "    display - real-time FPS and input monitor\n\n"
        "SYNOPSIS\n"
        "    display\n\n"
        "DESCRIPTION\n"
        "    Displays a fullscreen overlay with a live frames-per-second\n"
        "    counter, mouse position, button state, and a crosshair at\n"
        "    the current mouse coordinates. Useful for diagnosing input\n"
        "    and rendering issues. Press 'q' or ESC to exit.\n",
        CMD_FLAG_ROOT
    },
    {
        "test", cmd_test_expr,
        "Evaluate conditional expressions",
        "test: test EXPRESSION\n"
        "    Evaluate EXPRESSION and return 0 (true) or 1 (false).\n"
        "    File: -f FILE (exists, regular), -d FILE (is dir), -e FILE (exists)\n"
        "    String: -z STR (empty), -n STR (non-empty), S1 = S2, S1 != S2\n"
        "    Numeric: N1 -eq N2, -ne, -lt, -le, -gt, -ge\n",
        NULL, 0
    },
    {
        "[", cmd_test_expr,
        "Evaluate conditional expressions (alias for test)",
        "[: [ EXPRESSION ]\n"
        "    Same as 'test EXPRESSION'. The closing ] is required.\n",
        NULL, 0
    },
    {
        "true", cmd_true,
        "Return success exit code",
        "true: true\n"
        "    Always returns 0.\n",
        NULL, 0
    },
    {
        "false", cmd_false,
        "Return failure exit code",
        "false: false\n"
        "    Always returns 1.\n",
        NULL, 0
    },
    {
        "jobs", cmd_jobs,
        "List background jobs",
        "jobs: jobs\n"
        "    List all background and stopped jobs.\n",
        NULL, 0
    },
    {
        "fg", cmd_fg,
        "Bring job to foreground",
        "fg: fg [%N]\n"
        "    Bring job N to the foreground.\n",
        NULL, 0
    },
    {
        "bg", cmd_bg,
        "Resume job in background",
        "bg: bg [%N]\n"
        "    Resume stopped job N in the background.\n",
        NULL, 0
    },
    {
        "sleep", cmd_sleep,
        "Pause for specified seconds",
        "sleep: sleep SECONDS\n"
        "    Pause execution for SECONDS seconds.\n",
        NULL, 0
    },
};

const command_t *cmd_system_commands(int *count) {
    *count = sizeof(system_commands) / sizeof(system_commands[0]);
    return system_commands;
}
