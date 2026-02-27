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
#include <kernel/login.h>
#include <kernel/mouse.h>
#include <kernel/firewall.h>
#include <kernel/wm.h>
#include <kernel/ui_theme.h>
#include <kernel/state.h>
#include <kernel/env.h>
#include <kernel/fs.h>
#include <kernel/config.h>
#include <kernel/task.h>
#include <kernel/sched.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/clipboard.h>
#include <kernel/crypto.h>
#include <kernel/io.h>
#include <kernel/drm.h>

/* Routes putchar/getchar through serial COM1 instead of VGA/PS2 */
int g_serial_console = 0;

#define PROMPT      "$ "

#define CTRL_A  1
#define CTRL_C  3
#define CTRL_E  5
#define CTRL_K  11
#define CTRL_L  12
#define CTRL_U  21
#define CTRL_V  22
#define CTRL_W  23

/* --- Line editor state --- */
static char buf[SHELL_CMD_SIZE];
static size_t buf_len;
static size_t cursor;
static int hist_pos = -1;
static char saved_line[SHELL_CMD_SIZE];
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

/* ═══ Per-key Shell API ═══════════════════════════════════════════ */

void shell_init_interactive(void) {
    const char *home = env_get("HOME");
    if (home) fs_change_directory(home);
    else fs_change_directory("/home/root");
    printf("Type 'help' for a list of commands.\n");
}

void shell_draw_prompt(void) {
    buf_len = 0;
    cursor = 0;
    hist_pos = -1;
    print_prompt();
}

/* Process a single key for the line editor.
   Returns: 0 = continue (more input needed)
            1 = command ready (Enter pressed, buf is null-terminated)
            2 = redraw prompt (Ctrl+C or empty Enter) */
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
    if (c == CTRL_L) {
        terminal_clear();
        if (gfx_is_active()) desktop_draw_chrome();
        print_prompt();
        for (size_t i = 0; i < buf_len; i++) putchar(buf[i]);
        cursor_move((int)cursor - (int)buf_len);
        return 0;
    }
    if (c == '\b') {
        if (keyboard_get_ctrl()) {
            /* Ctrl+Backspace: delete word backward */
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
    if (c == KEY_LEFT) {
        if (cursor > 0) { cursor--; cursor_move(-1); }
        return 0;
    }
    if (c == KEY_RIGHT) {
        if (cursor < buf_len) { cursor++; cursor_move(1); }
        return 0;
    }
    if (c == KEY_HOME || c == CTRL_A) {
        cursor_move(-(int)cursor); cursor = 0;
        return 0;
    }
    if (c == KEY_END || c == CTRL_E) {
        cursor_move((int)buf_len - (int)cursor); cursor = buf_len;
        return 0;
    }
    if (c == CTRL_U) {
        if (cursor > 0) {
            /* Copy killed text to clipboard */
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
            /* Copy killed text to clipboard */
            clipboard_copy(&buf[cursor], buf_len - cursor);
            size_t removed = buf_len - cursor;
            buf_len = cursor; repaint_tail(removed);
        }
        return 0;
    }
    if (c == CTRL_V) {
        /* Paste from clipboard */
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
            cursor = new_len;
        }
        return 0;
    }
    if (c == KEY_ALT_TAB) {
        alttab_activate();
        while (alttab_is_visible()) {
            char tc = getchar();
            if (tc == KEY_ALT_TAB) {
                alttab_activate();
            } else if (tc == KEY_ESCAPE) {
                alttab_cancel();
            } else {
                alttab_confirm();
            }
        }
        return 0;
    }
    if (c == KEY_PGUP || c == KEY_PGDN || c == KEY_INS || c == KEY_ESCAPE || c == KEY_SUPER)
        return 0;

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

/* ═══ Blocking Shell Loop (backward compat for text-mode + desktop blocking) ═══ */

void shell_loop(void) {
    shell_init_interactive();

    while (1) {
        shell_draw_prompt();

        while (1) {
            char c = getchar();
            int r = shell_handle_key(c);
            if (r >= 1) break;
        }

        if (buf_len > 0) {
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

/* Multiboot module globals */
uint8_t *doom_wad_data = 0;
uint32_t doom_wad_size = 0;
uint8_t *initrd_data = 0;
uint32_t initrd_size = 0;

static int str_ends_with(const char *str, const char *suffix) {
    size_t slen = strlen(str);
    size_t sfxlen = strlen(suffix);
    if (sfxlen > slen) return 0;
    return strcmp(str + slen - sfxlen, suffix) == 0;
}

void kernel_main(multiboot_info_t* mbi) {
    /* Save cmdline BEFORE module malloc overwrites it (heap may overlap cmdline addr) */
    static char saved_cmdline[256];
    if ((mbi->flags & (1 << 2)) && mbi->cmdline) {
        const char *src = (const char *)mbi->cmdline;
        int i = 0;
        while (i < 255 && src[i]) { saved_cmdline[i] = src[i]; i++; }
        saved_cmdline[i] = '\0';
    }

    /* FIRST: Copy multiboot modules before any malloc overwrites them.
       GRUB places modules after the kernel in physical memory, which overlaps
       with our linear heap. Copy to malloc'd buffers immediately. */
    if ((mbi->flags & (1 << 3)) && mbi->mods_count > 0) {
        multiboot_module_t *mods = (multiboot_module_t *)mbi->mods_addr;
        for (uint32_t m = 0; m < mbi->mods_count; m++) {
            uint32_t mod_start = mods[m].mod_start;
            uint32_t mod_len = mods[m].mod_end - mods[m].mod_start;
            const char *cmdline = mods[m].cmdline ? (const char *)mods[m].cmdline : "";

            if (mod_len == 0 || mod_len >= 32 * 1024 * 1024) continue;

            uint8_t *copy = (uint8_t *)malloc(mod_len);
            if (!copy) continue;
            memcpy(copy, (uint8_t *)mod_start, mod_len);

            if (str_ends_with(cmdline, ".wad") || str_ends_with(cmdline, ".WAD")) {
                doom_wad_data = copy;
                doom_wad_size = mod_len;
            } else if (str_ends_with(cmdline, ".tar")) {
                initrd_data = copy;
                initrd_size = mod_len;
            } else {
                /* Try to identify by content */
                if (mod_len > 4 && copy[0] == 'I' && copy[1] == 'W' &&
                    copy[2] == 'A' && copy[3] == 'D') {
                    doom_wad_data = copy;
                    doom_wad_size = mod_len;
                } else {
                    /* Default: treat as initrd */
                    initrd_data = copy;
                    initrd_size = mod_len;
                }
            }
        }
    }

    /* Initialize serial debug output early so gfx_init can log */
    serial_init();
    DBG("ImposOS booting...");

    /* Check for terminal boot mode via kernel cmdline */
    int terminal_mode = strstr(saved_cmdline, "terminal") ? 1 : 0;
    DBG("cmdline='%s' terminal_mode=%d", saved_cmdline, terminal_mode);

    if (!terminal_mode) gfx_init(mbi);
    terminal_initialize();

    /* Set up GDT, IDT, PIC, PIT before anything else */
    idt_initialize();

    /* Initialize physical and virtual memory management */
    pmm_init(mbi);
    vmm_init(mbi);

    /* Print module info now that printf is safe */
    if (doom_wad_data && doom_wad_size > 0) {
        DBG("[BOOT] DOOM WAD: %u bytes at 0x%x (header: %c%c%c%c)",
            doom_wad_size, (uint32_t)doom_wad_data,
            doom_wad_data[0], doom_wad_data[1],
            doom_wad_data[2], doom_wad_data[3]);
    }
    if (initrd_data && initrd_size > 0) {
        DBG("[BOOT] Initrd: %u bytes at 0x%x",
            initrd_size, (uint32_t)initrd_data);
    }

    /* Initialize task tracking (before any tasks are created) */
    task_init();

    /* Initialize preemptive scheduler */
    sched_init();

    /* Initialize UI theme */
    ui_theme_init();

    /* Initialize CSPRNG (needs PIT + RTC + RDTSC) */
    prng_init();

    /* Initialize PS/2 mouse and firewall */
    mouse_initialize();
    firewall_initialize();

    ata_initialize();
    acpi_initialize();

    /* Detect GPU acceleration (VirtIO GPU + Bochs VGA BGA) */
    gfx_init_gpu_accel();

    /* Initialize DRM subsystem (GPU ioctl interface) */
    drm_init();

    if (gfx_is_active() && !terminal_mode) {
        /* Graphical boot: init subsystems, then run state machine */
        shell_initialize_subsystems();
        DBG("state_run: starting GUI");
        state_run();  /* Never returns */
    } else {
        /* Text-mode shell (either no framebuffer, or terminal boot mode) */
        shell_initialize();

        jmp_buf restart_point;
        exit_set_restart_point(&restart_point);

        if (setjmp(restart_point) != 0) {
            printf("\n");
            shell_login();
        }

        /* Text mode shell loop — uses the same per-key API */
        shell_loop();
    }
}
