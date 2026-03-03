/*
 * aarch64 kernel_main — Phase 6: Full subsystem parity.
 *
 * Boots to an interactive shell with filesystem, networking, crypto,
 * user management, and all serial-compatible shell commands.
 *
 * Also provides the line editor (shell_handle_key et al.) that on i386
 * lives in kernel/kernel/kernel.c.  For aarch64 we keep it here since
 * kernel.c is not compiled.
 */

#include <kernel/tty.h>
#include <kernel/io.h>
#include <kernel/boot_info.h>
#include <kernel/idt.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/task.h>
#include <kernel/sched.h>
#include <kernel/syscall.h>
#include <kernel/ata.h>
#include <kernel/pci.h>
#include <kernel/mouse.h>
#include <kernel/acpi.h>
#include <kernel/crypto.h>
#include <kernel/clipboard.h>
#include <kernel/env.h>
#include <kernel/fs.h>
#include <kernel/shell.h>
#include <string.h>
#include <stdio.h>

/* ═══ Globals that i386 kernel.c provides ═══════════════════════════ */

boot_info_t  g_boot_info;
int          g_serial_console = 1;   /* always serial on aarch64 */
uint8_t     *initrd_data = 0;
uint32_t     initrd_size = 0;

/* ═══ Stubs for GUI / desktop (never used on serial) ════════════════ */

int      gfx_is_active(void)              { return 0; }
uint32_t gfx_get_system_ram_mb(void)      { return 256; }
void     desktop_draw_chrome(void)        {}
void     desktop_get_idle_terminal_cb(void) {}
void     wm_composite(void)              {}

/* Alt-tab (desktop window switcher — no-ops) */
void alttab_activate(void)  {}
void alttab_confirm(void)   {}
void alttab_cancel(void)    {}
int  alttab_is_visible(void){ return 0; }

/* ═══ Line editor (mirrors kernel/kernel/kernel.c) ══════════════════
 *
 * Provides the per-key shell API declared in shell.h:
 *   shell_init_interactive, shell_draw_prompt,
 *   shell_handle_key, shell_get_command
 */

#define CTRL_A  1
#define CTRL_C  3
#define CTRL_E  5
#define CTRL_K  11
#define CTRL_L  12
#define CTRL_U  21
#define CTRL_V  22
#define CTRL_W  23
#define CTRL_Z  26

static char   buf[SHELL_CMD_SIZE];
static size_t buf_len;
static size_t cursor;
static int    hist_pos = -1;
static char   saved_line[SHELL_CMD_SIZE];
static volatile size_t saved_len;

static void cursor_move(int delta) {
    size_t w = terminal_get_width();
    int abs_pos = (int)(terminal_get_row() * w + terminal_get_column()) + delta;
    if (abs_pos < 0) abs_pos = 0;
    terminal_set_cursor((size_t)(abs_pos % w), (size_t)(abs_pos / w));
}

static void repaint_tail(size_t clear_n) {
    for (size_t i = cursor; i < buf_len; i++)
        putchar(buf[i]);
    for (size_t i = 0; i < clear_n; i++)
        putchar(' ');
    cursor_move((int)cursor - (int)(buf_len + clear_n));
}

static void line_replace(const char *text, size_t len, size_t old_len) {
    memcpy(buf, text, len);
    buf_len = len;
    for (size_t i = 0; i < buf_len; i++)
        putchar(buf[i]);
    size_t clear = (old_len > buf_len) ? (old_len - buf_len) : 0;
    for (size_t i = 0; i < clear; i++)
        putchar(' ');
    cursor_move(-(int)clear);
    cursor = buf_len;
}

/* Prompt — simplified for serial (no VGA colors, uses ANSI if supported) */
static void print_prompt(void) {
    const char *ps1 = env_get("PS1");
    if (!ps1) ps1 = "$ ";
    const char *p = ps1;
    while (*p) {
        if (*p == '\\' && *(p + 1) == 'w') {
            const char *cwd = fs_get_cwd();
            const char *home = env_get("HOME");
            if (home && strncmp(cwd, home, strlen(home)) == 0) {
                const char *relative = cwd + strlen(home);
                if (*relative == '/') relative++;
                if (*relative) printf("%s", relative);
            } else {
                printf("%s", cwd);
            }
            p += 2;
            continue;
        }
        putchar(*p);
        p++;
    }
}

void shell_init_interactive(void) {
    const char *home = env_get("HOME");
    if (home) fs_change_directory(home);
    else fs_change_directory("/home/root");
    printf("Type 'help' for a list of commands.\n");
}

void shell_draw_prompt(void) {
    buf_len = 0;
    cursor  = 0;
    hist_pos = -1;
    print_prompt();
}

int shell_handle_key(char c) {
    if (c == '\n') {
        buf[buf_len] = '\0';
        cursor_move((int)buf_len - (int)cursor);
        printf("\n");
        if (buf_len == 0) return 2;
        return 1;
    }
    if (c == CTRL_C) {
        cursor_move((int)buf_len - (int)cursor);
        printf("^C\n");
        return 2;
    }
    if (c == CTRL_Z) {
        shell_fg_app_t *fg = shell_get_fg_app();
        if (fg) {
            printf("^Z\n");
            shell_suspend_fg_app();
            return 2;
        }
        return 0;
    }
    if (c == CTRL_L) {
        terminal_clear();
        print_prompt();
        for (size_t i = 0; i < buf_len; i++) putchar(buf[i]);
        cursor_move((int)cursor - (int)buf_len);
        return 0;
    }
    if (c == '\b') {
        if (keyboard_get_ctrl()) {
            if (cursor > 0) {
                size_t new_cur = cursor;
                while (new_cur > 0 && buf[new_cur - 1] == ' ') new_cur--;
                while (new_cur > 0 && buf[new_cur - 1] != ' ') new_cur--;
                size_t removed = cursor - new_cur;
                memmove(&buf[new_cur], &buf[cursor], buf_len - cursor);
                buf_len -= removed; cursor_move(-(int)removed);
                cursor = new_cur; repaint_tail(removed);
            }
        } else if (cursor > 0) {
            memmove(&buf[cursor - 1], &buf[cursor], buf_len - cursor);
            buf_len--; cursor_move(-1); cursor--;
            repaint_tail(1);
        }
        return 0;
    }
    if (c == KEY_DEL) {
        if (cursor < buf_len) {
            memmove(&buf[cursor], &buf[cursor + 1], buf_len - cursor - 1);
            buf_len--; repaint_tail(1);
        }
        return 0;
    }
    if (c == KEY_LEFT)  { if (cursor > 0)       { cursor--; cursor_move(-1); } return 0; }
    if (c == KEY_RIGHT) { if (cursor < buf_len)  { cursor++; cursor_move(1);  } return 0; }
    if (c == KEY_HOME || c == CTRL_A) { cursor_move(-(int)cursor); cursor = 0; return 0; }
    if (c == KEY_END  || c == CTRL_E) { cursor_move((int)buf_len - (int)cursor); cursor = buf_len; return 0; }
    if (c == CTRL_U) {
        if (cursor > 0) {
            clipboard_copy(buf, cursor);
            size_t removed = cursor;
            memmove(buf, &buf[cursor], buf_len - cursor);
            buf_len -= removed; cursor_move(-(int)removed);
            cursor = 0; repaint_tail(removed);
        }
        return 0;
    }
    if (c == CTRL_K) {
        if (cursor < buf_len) {
            clipboard_copy(&buf[cursor], buf_len - cursor);
            size_t removed = buf_len - cursor;
            buf_len = cursor; repaint_tail(removed);
        }
        return 0;
    }
    if (c == CTRL_V) {
        size_t clip_len;
        const char *clip = clipboard_get(&clip_len);
        if (clip && clip_len > 0) {
            for (size_t i = 0; i < clip_len && buf_len < SHELL_CMD_SIZE - 1; i++) {
                char ch = clip[i];
                if (ch < 32 || ch >= 127) continue;
                memmove(&buf[cursor + 1], &buf[cursor], buf_len - cursor);
                buf[cursor] = ch;
                cursor++; buf_len++;
                putchar(ch);
            }
            repaint_tail(0);
        }
        return 0;
    }
    if (c == CTRL_W) {
        if (cursor > 0) {
            size_t new_cur = cursor;
            while (new_cur > 0 && buf[new_cur - 1] == ' ') new_cur--;
            while (new_cur > 0 && buf[new_cur - 1] != ' ') new_cur--;
            size_t removed = cursor - new_cur;
            memmove(&buf[new_cur], &buf[cursor], buf_len - cursor);
            buf_len -= removed; cursor_move(-(int)removed);
            cursor = new_cur; repaint_tail(removed);
        }
        return 0;
    }
    if (c == KEY_UP) {
        int hcount = shell_history_count();
        int target;
        if (hist_pos == -1) {
            if (hcount == 0) return 0;
            memcpy(saved_line, buf, buf_len);
            saved_len = buf_len;
            target = hcount - 1;
        } else {
            target = hist_pos - 1;
        }
        int oldest = (hcount > SHELL_HIST_SIZE) ? (hcount - SHELL_HIST_SIZE) : 0;
        if (target < oldest) return 0;
        hist_pos = target;
        size_t old_len = buf_len;
        cursor_move(-(int)cursor); cursor = 0;
        const char *entry = shell_history_entry(hist_pos);
        line_replace(entry, strlen(entry), old_len);
        return 0;
    }
    if (c == KEY_DOWN) {
        if (hist_pos == -1) return 0;
        size_t old_len = buf_len;
        hist_pos++;
        cursor_move(-(int)cursor); cursor = 0;
        if (hist_pos >= shell_history_count()) {
            hist_pos = -1;
            line_replace(saved_line, saved_len, old_len);
        } else {
            const char *entry = shell_history_entry(hist_pos);
            line_replace(entry, strlen(entry), old_len);
        }
        return 0;
    }
    if (c == '\t') {
        buf[buf_len] = '\0';
        size_t old_len = buf_len;
        size_t new_len = shell_autocomplete(buf, buf_len, SHELL_CMD_SIZE);
        if (new_len != old_len) {
            size_t word_start = cursor;
            while (word_start > 0 && buf[word_start - 1] != ' ')
                word_start--;
            size_t old_word_len = old_len - word_start;
            if (cursor > word_start)
                cursor_move((int)word_start - (int)cursor);
            for (size_t i = 0; i < old_word_len; i++) putchar(' ');
            for (size_t i = 0; i < old_word_len; i++) putchar('\b');
            for (size_t i = word_start; i < new_len; i++) putchar(buf[i]);
            buf_len = new_len;
            cursor  = new_len;
        }
        return 0;
    }
    /* Ignore GUI-only or non-printable keys */
    if (c == KEY_ALT_TAB || c == KEY_PGUP || c == KEY_PGDN ||
        c == KEY_INS || c == KEY_ESCAPE || c == KEY_SUPER)
        return 0;

    /* Printable character — insert at cursor */
    if (buf_len < SHELL_CMD_SIZE - 1) {
        if (cursor < buf_len)
            memmove(&buf[cursor + 1], &buf[cursor], buf_len - cursor);
        buf[cursor] = c;
        buf_len++;
        putchar(c);
        cursor++;
        if (cursor < buf_len) repaint_tail(0);
    }
    return 0;
}

const char *shell_get_command(void) {
    return buf;
}

/* ═══ Shell thread ══════════════════════════════════════════════════ */

static void shell_thread(void) {
    printf("\r\n");
    printf("========================================\r\n");
    printf("  ImposOS aarch64 — Phase 6\r\n");
    printf("  Full subsystem parity (serial)\r\n");
    printf("========================================\r\n");
    printf("\r\n");

    /*
     * shell_initialize() → shell_initialize_subsystems() → fs, config,
     * rtc, net, env, hostname, user, group, quota (fs_initialize()
     * internally calls vfs_init, procfs_init, devfs_init, tmpfs_init)
     * Then shell_build_command_table() + user setup wizard.
     */
    shell_initialize();

    /* Login + interactive shell loop */
    while (1) {
        if (shell_login() < 0)
            continue;

        shell_init_interactive();
        shell_draw_prompt();

        while (1) {
            char c = getchar();
            int result = shell_handle_key(c);

            if (result == 1) {
                const char *cmd = shell_get_command();
                if (cmd && cmd[0]) {
                    shell_history_add(cmd);
                    shell_process_command((char *)cmd);
                }
                shell_draw_prompt();
            } else if (result == 2) {
                shell_draw_prompt();
            }
        }
    }
}

/* ═══ Kernel entry ══════════════════════════════════════════════════ */

void kernel_main(void *dtb) {
    g_boot_info.arch_data = dtb;
    serial_init();
    terminal_initialize();

    DBG("DTB pointer: 0x%x", (unsigned)(uintptr_t)dtb);

    /* Core infrastructure */
    pmm_init(0);
    vmm_init(0);
    idt_initialize();

    /* Tasks + scheduler */
    task_init();
    sched_init();
    DBG("Scheduler active, %d tasks", task_count());

    /* ACPI (PSCI) + PCI + mouse stubs */
    acpi_initialize();
    pci_initialize();
    mouse_initialize();

    /* Block device + PRNG (before shell, like i386 kernel.c) */
    ata_initialize();
    prng_init();

    /* Create shell thread and let scheduler run it */
    task_create_thread("shell", shell_thread, 0);

    /* Idle loop */
    while (1) {
        cpu_halting = 1;
        __asm__ volatile("wfi");
        cpu_halting = 0;
    }
}
