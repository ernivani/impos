#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <kernel/tty.h>
#include <kernel/vga.h>
#include <kernel/shell.h>
#include <kernel/ata.h>
#include <kernel/acpi.h>
#include <kernel/idt.h>
#include <kernel/multiboot.h>
#include <kernel/gfx.h>
#include <kernel/desktop.h>
#include <kernel/mouse.h>
#include <kernel/firewall.h>
#include <kernel/wm.h>
#include <kernel/ui_theme.h>
#include <kernel/filemgr.h>
#include <kernel/taskmgr.h>
#include <kernel/settings_app.h>
#include <kernel/monitor_app.h>

#define PROMPT      "$ "

#define CTRL_A  1
#define CTRL_C  3
#define CTRL_E  5
#define CTRL_K  11
#define CTRL_L  12
#define CTRL_U  21
#define CTRL_W  23

/* --- Line editor state --- */
static char buf[SHELL_CMD_SIZE];
static size_t buf_len;
static size_t cursor;

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

/* Print PS1 prompt with colored segments and \w expansion */
static void print_prompt(void) {
    const char* ps1 = env_get("PS1");
    if (!ps1) ps1 = "$ ";
    const char* p = ps1;
    int in_username = 0, in_hostname = 0, in_path = 0;

    while (*p) {
        if (p == ps1 || (p > ps1 && *(p-1) == '\n'))
            in_username = 1;
        if (*p == '@' && in_username) {
            in_username = 0; in_hostname = 1;
            terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            putchar('@');
            terminal_setcolor(VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK);
            p++; continue;
        }
        if (*p == ':' && in_hostname) {
            in_hostname = 0; in_path = 1;
            terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            putchar(':');
            terminal_setcolor(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
            p++; continue;
        }
        if (in_username && !in_hostname && !in_path)
            terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        else if (in_hostname)
            terminal_setcolor(VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK);
        else if (in_path)
            terminal_setcolor(VGA_COLOR_CYAN, VGA_COLOR_BLACK);

        if (*p == '\\' && *(p + 1) == 'w') {
            const char* cwd = fs_get_cwd();
            const char* home = env_get("HOME");
            if (home && strncmp(cwd, home, strlen(home)) == 0) {
                const char* relative = cwd + strlen(home);
                if (*relative == '/') relative++;
                if (*relative) printf("%s", relative);
            } else {
                printf("%s", cwd);
            }
            p += 2; continue;
        }
        if ((*p == '$' || *p == '#') && *(p+1) == ' ' && *(p+2) == '\0')
            terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        putchar(*p);
        p++;
    }
    terminal_resetcolor();
}

/* Shell input loop — runs until exit or Ctrl+D */
static void shell_loop(void) {
    const char* home = env_get("HOME");
    if (home) fs_change_directory(home);
    else fs_change_directory("/home/root");

    printf("Type 'help' for a list of commands.\n");

    int hist_pos;
    char saved_line[SHELL_CMD_SIZE];
    volatile size_t saved_len;
    int cancelled;

    while (1) {
        print_prompt();
        buf_len = 0;
        cursor = 0;
        hist_pos = -1;
        cancelled = 0;

        while (1) {
            char c = getchar();

            if (c == '\n') {
                buf[buf_len] = '\0';
                cursor_move((int)buf_len - (int)cursor);
                printf("\n");
                break;
            }
            if (c == CTRL_C) {
                cursor_move((int)buf_len - (int)cursor);
                printf("^C\n");
                cancelled = 1;
                break;
            }
            if (c == CTRL_L) {
                terminal_clear();
                if (gfx_is_active()) desktop_draw_chrome();
                print_prompt();
                for (size_t i = 0; i < buf_len; i++) putchar(buf[i]);
                cursor_move((int)cursor - (int)buf_len);
                continue;
            }
            if (c == '\b') {
                if (cursor > 0) {
                    memmove(&buf[cursor - 1], &buf[cursor], buf_len - cursor);
                    buf_len--; cursor_move(-1); cursor--;
                    repaint_tail(1);
                }
                continue;
            }
            if (c == KEY_DEL) {
                if (cursor < buf_len) {
                    memmove(&buf[cursor], &buf[cursor + 1], buf_len - cursor - 1);
                    buf_len--; repaint_tail(1);
                }
                continue;
            }
            if (c == KEY_LEFT) {
                if (cursor > 0) { cursor--; cursor_move(-1); }
                continue;
            }
            if (c == KEY_RIGHT) {
                if (cursor < buf_len) { cursor++; cursor_move(1); }
                continue;
            }
            if (c == KEY_HOME || c == CTRL_A) {
                cursor_move(-(int)cursor); cursor = 0;
                continue;
            }
            if (c == KEY_END || c == CTRL_E) {
                cursor_move((int)buf_len - (int)cursor); cursor = buf_len;
                continue;
            }
            if (c == CTRL_U) {
                if (cursor > 0) {
                    size_t removed = cursor;
                    memmove(buf, &buf[cursor], buf_len - cursor);
                    buf_len -= removed; cursor_move(-(int)removed);
                    cursor = 0; repaint_tail(removed);
                }
                continue;
            }
            if (c == CTRL_K) {
                if (cursor < buf_len) {
                    size_t removed = buf_len - cursor;
                    buf_len = cursor; repaint_tail(removed);
                }
                continue;
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
                continue;
            }
            if (c == KEY_UP) {
                int hcount = shell_history_count();
                int target;
                if (hist_pos == -1) {
                    if (hcount == 0) continue;
                    memcpy(saved_line, buf, buf_len);
                    saved_len = buf_len;
                    target = hcount - 1;
                } else {
                    target = hist_pos - 1;
                }
                int oldest = (hcount > SHELL_HIST_SIZE) ? (hcount - SHELL_HIST_SIZE) : 0;
                if (target < oldest) continue;
                hist_pos = target;
                size_t old_len = buf_len;
                cursor_move(-(int)cursor); cursor = 0;
                const char *entry = shell_history_entry(hist_pos);
                line_replace(entry, strlen(entry), old_len);
                continue;
            }
            if (c == KEY_DOWN) {
                if (hist_pos == -1) continue;
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
                continue;
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
                    cursor = new_len;
                }
                continue;
            }
            if (c == KEY_ALT_TAB) {
                wm_cycle_focus();
                continue;
            }
            if (c == KEY_PGUP || c == KEY_PGDN || c == KEY_INS || c == KEY_ESCAPE || c == KEY_SUPER)
                continue;

            if (buf_len < SHELL_CMD_SIZE - 1) {
                if (cursor < buf_len)
                    memmove(&buf[cursor + 1], &buf[cursor], buf_len - cursor);
                buf[cursor] = c;
                buf_len++;
                putchar(c);
                cursor++;
                if (cursor < buf_len) repaint_tail(0);
            }
        }

        if (!cancelled && buf_len > 0) {
            shell_history_add(buf);
            config_tick_second();
            shell_process_command(buf);
            if (shell_exit_requested) {
                shell_exit_requested = 0;
                return;
            }
        }
    }
}

void kernel_main(multiboot_info_t* mbi) {
    gfx_init(mbi);
    terminal_initialize();

    /* Set up GDT, IDT, PIC, PIT before anything else */
    idt_initialize();

    /* Initialize UI theme */
    ui_theme_init();

    /* Initialize PS/2 mouse and firewall */
    mouse_initialize();
    firewall_initialize();

    if (gfx_is_active())
        desktop_splash();

    ata_initialize();
    acpi_initialize();

    if (gfx_is_active()) {
        /* Graphical boot: init subsystems, setup/login, then desktop */
        shell_initialize_subsystems();

        if (shell_needs_setup())
            desktop_setup();
        else
            desktop_login();

        /* Set up restart point for logout */
        jmp_buf restart_point;
        exit_set_restart_point(&restart_point);

        if (setjmp(restart_point) != 0) {
            /* Returned from logout — re-authenticate */
            desktop_login();
        }

        /* Desktop event loop */
        while (1) {
            int action = desktop_run();

            switch (action) {
            case DESKTOP_ACTION_TERMINAL:
                desktop_open_terminal();
                shell_loop();
                desktop_close_terminal();
                break;

            case DESKTOP_ACTION_EDITOR:
                desktop_open_terminal();
                printf("vi: open a file with 'vi <filename>'\n");
                shell_loop();
                desktop_close_terminal();
                break;

            case DESKTOP_ACTION_FILES:
                app_filemgr();
                break;

            case DESKTOP_ACTION_BROWSER:
                app_taskmgr();
                break;

            case DESKTOP_ACTION_SETTINGS:
                app_settings();
                break;

            case DESKTOP_ACTION_MONITOR:
                app_monitor();
                break;

            case DESKTOP_ACTION_POWER:
                /* Trigger logout via longjmp */
                exit(0);
                break;

            default:
                break;
            }
        }
    } else {
        /* Text-mode fallback — original flow */
        shell_initialize();

        jmp_buf restart_point;
        exit_set_restart_point(&restart_point);

        if (setjmp(restart_point) != 0) {
            printf("\n");
            shell_login();
        }

        /* Text mode shell loop */
        const char* home = env_get("HOME");
        if (home) fs_change_directory(home);
        else fs_change_directory("/home/root");

        int hist_pos;
        char saved_line[SHELL_CMD_SIZE];
        volatile size_t saved_len;
        int cancelled;

        while (1) {
            print_prompt();
            buf_len = 0;
            cursor = 0;
            hist_pos = -1;
            cancelled = 0;

            while (1) {
                char c = getchar();
                if (c == '\n') {
                    buf[buf_len] = '\0';
                    cursor_move((int)buf_len - (int)cursor);
                    printf("\n"); break;
                }
                if (c == CTRL_C) {
                    cursor_move((int)buf_len - (int)cursor);
                    printf("^C\n"); cancelled = 1; break;
                }
                if (c == CTRL_L) {
                    terminal_clear();
                    print_prompt();
                    for (size_t i = 0; i < buf_len; i++) putchar(buf[i]);
                    cursor_move((int)cursor - (int)buf_len);
                    continue;
                }
                if (c == '\b') {
                    if (cursor > 0) {
                        memmove(&buf[cursor-1], &buf[cursor], buf_len-cursor);
                        buf_len--; cursor_move(-1); cursor--;
                        repaint_tail(1);
                    }
                    continue;
                }
                if (c == KEY_DEL) {
                    if (cursor < buf_len) {
                        memmove(&buf[cursor], &buf[cursor+1], buf_len-cursor-1);
                        buf_len--; repaint_tail(1);
                    }
                    continue;
                }
                if (c == KEY_LEFT) { if (cursor > 0) { cursor--; cursor_move(-1); } continue; }
                if (c == KEY_RIGHT) { if (cursor < buf_len) { cursor++; cursor_move(1); } continue; }
                if (c == KEY_HOME || c == CTRL_A) { cursor_move(-(int)cursor); cursor = 0; continue; }
                if (c == KEY_END || c == CTRL_E) { cursor_move((int)buf_len-(int)cursor); cursor = buf_len; continue; }
                if (c == CTRL_U) {
                    if (cursor > 0) {
                        size_t removed = cursor;
                        memmove(buf, &buf[cursor], buf_len-cursor);
                        buf_len -= removed; cursor_move(-(int)removed);
                        cursor = 0; repaint_tail(removed);
                    }
                    continue;
                }
                if (c == CTRL_K) {
                    if (cursor < buf_len) { size_t removed = buf_len-cursor; buf_len = cursor; repaint_tail(removed); }
                    continue;
                }
                if (c == CTRL_W) {
                    if (cursor > 0) {
                        size_t new_cur = cursor;
                        while (new_cur > 0 && buf[new_cur-1] == ' ') new_cur--;
                        while (new_cur > 0 && buf[new_cur-1] != ' ') new_cur--;
                        size_t removed = cursor - new_cur;
                        memmove(&buf[new_cur], &buf[cursor], buf_len-cursor);
                        buf_len -= removed; cursor_move(-(int)removed);
                        cursor = new_cur; repaint_tail(removed);
                    }
                    continue;
                }
                if (c == KEY_UP) {
                    int hcount = shell_history_count();
                    int target;
                    if (hist_pos == -1) {
                        if (hcount == 0) continue;
                        memcpy(saved_line, buf, buf_len); saved_len = buf_len;
                        target = hcount - 1;
                    } else { target = hist_pos - 1; }
                    int oldest = (hcount > SHELL_HIST_SIZE) ? (hcount - SHELL_HIST_SIZE) : 0;
                    if (target < oldest) continue;
                    hist_pos = target;
                    size_t old_len = buf_len;
                    cursor_move(-(int)cursor); cursor = 0;
                    line_replace(shell_history_entry(hist_pos), strlen(shell_history_entry(hist_pos)), old_len);
                    continue;
                }
                if (c == KEY_DOWN) {
                    if (hist_pos == -1) continue;
                    size_t old_len = buf_len; hist_pos++;
                    cursor_move(-(int)cursor); cursor = 0;
                    if (hist_pos >= shell_history_count()) {
                        hist_pos = -1; line_replace(saved_line, saved_len, old_len);
                    } else {
                        const char *entry = shell_history_entry(hist_pos);
                        line_replace(entry, strlen(entry), old_len);
                    }
                    continue;
                }
                if (c == '\t') {
                    buf[buf_len] = '\0';
                    size_t old_len = buf_len;
                    size_t new_len = shell_autocomplete(buf, buf_len, SHELL_CMD_SIZE);
                    if (new_len != old_len) {
                        size_t word_start = cursor;
                        while (word_start > 0 && buf[word_start-1] != ' ') word_start--;
                        size_t old_word_len = old_len - word_start;
                        if (cursor > word_start) cursor_move((int)word_start-(int)cursor);
                        for (size_t i = 0; i < old_word_len; i++) putchar(' ');
                        for (size_t i = 0; i < old_word_len; i++) putchar('\b');
                        for (size_t i = word_start; i < new_len; i++) putchar(buf[i]);
                        buf_len = new_len; cursor = new_len;
                    }
                    continue;
                }
                if (c == KEY_PGUP || c == KEY_PGDN || c == KEY_INS || c == KEY_ESCAPE || c == KEY_ALT_TAB || c == KEY_SUPER) continue;
                if (buf_len < SHELL_CMD_SIZE - 1) {
                    if (cursor < buf_len) memmove(&buf[cursor+1], &buf[cursor], buf_len-cursor);
                    buf[cursor] = c; buf_len++; putchar(c); cursor++;
                    if (cursor < buf_len) repaint_tail(0);
                }
            }

            if (!cancelled && buf_len > 0) {
                shell_history_add(buf);
                config_tick_second();
                shell_process_command(buf);
            }
        }
    }
}
