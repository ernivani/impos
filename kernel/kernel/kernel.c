#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <kernel/tty.h>
#include <kernel/shell.h>
#include <kernel/ata.h>

#define PROMPT      "$ "

/* Ctrl key codes (Ctrl+letter = letter - 'a' + 1) */
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
    int abs_pos = (int)(terminal_get_row() * 80 + terminal_get_column()) + delta;
    if (abs_pos < 0) abs_pos = 0;
    terminal_set_cursor((size_t)(abs_pos % 80), (size_t)(abs_pos / 80));
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

void kernel_main(void) {
    terminal_initialize();
    
    /* Initialize ATA disk driver */
    ata_initialize();
    
    shell_initialize();

    /* Set up restart point for exit() to return here */
    jmp_buf restart_point;
    exit_set_restart_point(&restart_point);
    
    if (setjmp(restart_point) != 0) {
        /* We've returned from exit(), restart the shell */
        printf("\n");
    }
    
    /* Reset to home directory at shell start */
    fs_change_directory("/home/root");

    int hist_pos;
    char saved_line[SHELL_CMD_SIZE];
    volatile size_t saved_len;
    int cancelled;

    while (1) {
        printf(PROMPT);
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
                printf(PROMPT);
                for (size_t i = 0; i < buf_len; i++)
                    putchar(buf[i]);
                cursor_move((int)cursor - (int)buf_len);
                continue;
            }

            if (c == '\b') {
                if (cursor > 0) {
                    memmove(&buf[cursor - 1], &buf[cursor], buf_len - cursor);
                    buf_len--;
                    cursor_move(-1);
                    cursor--;
                    repaint_tail(1);
                }
                continue;
            }

            if (c == KEY_DEL) {
                if (cursor < buf_len) {
                    memmove(&buf[cursor], &buf[cursor + 1], buf_len - cursor - 1);
                    buf_len--;
                    repaint_tail(1);
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
                cursor_move(-(int)cursor);
                cursor = 0;
                continue;
            }

            if (c == KEY_END || c == CTRL_E) {
                cursor_move((int)buf_len - (int)cursor);
                cursor = buf_len;
                continue;
            }

            if (c == CTRL_U) {
                if (cursor > 0) {
                    size_t removed = cursor;
                    memmove(buf, &buf[cursor], buf_len - cursor);
                    buf_len -= removed;
                    cursor_move(-(int)removed);
                    cursor = 0;
                    repaint_tail(removed);
                }
                continue;
            }

            if (c == CTRL_K) {
                if (cursor < buf_len) {
                    size_t removed = buf_len - cursor;
                    buf_len = cursor;
                    repaint_tail(removed);
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
                    buf_len -= removed;
                    cursor_move(-(int)removed);
                    cursor = new_cur;
                    repaint_tail(removed);
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
                cursor_move(-(int)cursor);
                cursor = 0;
                const char *entry = shell_history_entry(hist_pos);
                line_replace(entry, strlen(entry), old_len);
                continue;
            }

            if (c == KEY_DOWN) {
                if (hist_pos == -1) continue;
                size_t old_len = buf_len;
                hist_pos++;
                cursor_move(-(int)cursor);
                cursor = 0;
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
                    // Find start of the word that was completed (last space before cursor)
                    size_t word_start = cursor;
                    while (word_start > 0 && buf[word_start - 1] != ' ') {
                        word_start--;
                    }
                    
                    size_t old_word_len = old_len - word_start;
                    size_t new_word_len = new_len - word_start;
                    
                    // Move cursor to start of word
                    if (cursor > word_start) {
                        cursor_move((int)word_start - (int)cursor);
                    }
                    
                    // Clear old word with spaces then backspace
                    for (size_t i = 0; i < old_word_len; i++)
                        putchar(' ');
                    for (size_t i = 0; i < old_word_len; i++)
                        putchar('\b');
                    
                    // Write new word
                    for (size_t i = word_start; i < new_len; i++)
                        putchar(buf[i]);
                    
                    buf_len = new_len;
                    cursor = new_len;
                }
                continue;
            }

            if (c == KEY_PGUP || c == KEY_PGDN || c == KEY_INS || c == KEY_ESCAPE)
                continue;

            if (buf_len < SHELL_CMD_SIZE - 1) {
                if (cursor < buf_len)
                    memmove(&buf[cursor + 1], &buf[cursor], buf_len - cursor);
                buf[cursor] = c;
                buf_len++;
                putchar(c);
                cursor++;
                if (cursor < buf_len)
                    repaint_tail(0);
            }
        }

        if (!cancelled && buf_len > 0) {
            shell_history_add(buf);
            config_tick_second();  // Approximate: 1 second per command
            shell_process_command(buf);
        }
    }
}
