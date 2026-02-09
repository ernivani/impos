#include <kernel/vi.h>
#include <kernel/fs.h>
#include <kernel/tty.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define VI_ROWS      24
#define VI_COLS      80
#define VI_STATUS    24
#define VI_MAX_LINES 512
#define VI_LINE_LEN  256
#define VGA_W        80
#define VGA_H        25

typedef enum { NORMAL, INSERT, COMMAND } vi_mode_t;

static uint16_t* const vga = (uint16_t*)0xB8000;
static const uint8_t COL_TEXT   = 0x07;
static const uint8_t COL_TILDE = 0x01;
static const uint8_t COL_BAR   = 0x70;

static char lines[VI_MAX_LINES][VI_LINE_LEN];
static int  num_lines;
static int  cx, cy;
static int  scroll_off;
static vi_mode_t mode;
static char cmd_buf[80];
static int  cmd_len;
static char vi_fname[28];
static int  modified;
static int  running;
static int  pending_d;
static int  pending_g;
static char msg[80];

static inline void vi_outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void vi_putc(int row, int col, char c, uint8_t color) {
    vga[row * VGA_W + col] = (uint16_t)(unsigned char)c | ((uint16_t)color << 8);
}

static void vi_set_cursor(int row, int col) {
    uint16_t pos = (uint16_t)(row * VGA_W + col);
    vi_outb(0x3D4, 14);
    vi_outb(0x3D5, (uint8_t)(pos >> 8));
    vi_outb(0x3D4, 15);
    vi_outb(0x3D5, (uint8_t)(pos & 0xFF));
}

static int vi_draw_str(int row, int col, const char* s, uint8_t color) {
    while (*s && col < VGA_W) {
        vi_putc(row, col++, *s++, color);
    }
    return col;
}

static int vi_itoa(int val, char* buf) {
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return 1; }
    char tmp[12];
    int i = 0, neg = 0;
    if (val < 0) { neg = 1; val = -val; }
    while (val > 0) { tmp[i++] = '0' + val % 10; val /= 10; }
    int len = 0;
    if (neg) buf[len++] = '-';
    while (i > 0) buf[len++] = tmp[--i];
    buf[len] = '\0';
    return len;
}

static void vi_clamp(void) {
    if (cy < 0) cy = 0;
    if (cy >= num_lines) cy = num_lines - 1;
    int len = (int)strlen(lines[cy]);
    if (mode == INSERT) {
        if (cx > len) cx = len;
    } else {
        if (len == 0) cx = 0;
        else if (cx >= len) cx = len - 1;
    }
    if (cx < 0) cx = 0;
}

static void vi_scroll(void) {
    if (cy < scroll_off)
        scroll_off = cy;
    if (cy >= scroll_off + VI_ROWS)
        scroll_off = cy - VI_ROWS + 1;
}

static void vi_draw_status(void) {
    int col;
    for (col = 0; col < VGA_W; col++)
        vi_putc(VI_STATUS, col, ' ', COL_BAR);

    if (mode == COMMAND) {
        col = vi_draw_str(VI_STATUS, 0, ":", COL_BAR);
        for (int i = 0; i < cmd_len; i++)
            vi_putc(VI_STATUS, col++, cmd_buf[i], COL_BAR);
        return;
    }

    col = 0;
    if (msg[0]) {
        col = vi_draw_str(VI_STATUS, col, msg, COL_BAR);
    } else {
        if (mode == INSERT)
            col = vi_draw_str(VI_STATUS, col, "-- INSERT -- ", COL_BAR);
        col = vi_draw_str(VI_STATUS, col, vi_fname, COL_BAR);
        if (modified)
            col = vi_draw_str(VI_STATUS, col, " [+]", COL_BAR);
    }

    char num[12];
    char right[40];
    int rp = 0;
    const char* s;
    s = "Ln "; while (*s) right[rp++] = *s++;
    int n = vi_itoa(cy + 1, num);
    for (int i = 0; i < n; i++) right[rp++] = num[i];
    right[rp++] = ','; right[rp++] = ' ';
    s = "Col "; while (*s) right[rp++] = *s++;
    n = vi_itoa(cx + 1, num);
    for (int i = 0; i < n; i++) right[rp++] = num[i];
    right[rp] = '\0';

    int start = VGA_W - rp - 1;
    if (start > col)
        vi_draw_str(VI_STATUS, start, right, COL_BAR);
}

static void vi_draw(void) {
    for (int row = 0; row < VI_ROWS; row++) {
        int li = scroll_off + row;
        if (li < num_lines) {
            int len = (int)strlen(lines[li]);
            int col;
            for (col = 0; col < VGA_W; col++) {
                if (col < len)
                    vi_putc(row, col, lines[li][col], COL_TEXT);
                else
                    vi_putc(row, col, ' ', COL_TEXT);
            }
        } else {
            vi_putc(row, 0, '~', COL_TILDE);
            for (int col = 1; col < VGA_W; col++)
                vi_putc(row, col, ' ', COL_TEXT);
        }
    }
    vi_draw_status();

    if (mode == COMMAND)
        vi_set_cursor(VI_STATUS, 1 + cmd_len);
    else
        vi_set_cursor(cy - scroll_off, cx);
}

static void vi_load(void) {
    uint8_t* buf = (uint8_t*)malloc(MAX_FILE_SIZE);
    if (!buf) {
        num_lines = 1;
        lines[0][0] = '\0';
        return;
    }
    size_t size;

    num_lines = 0;
    memset(lines[0], 0, VI_LINE_LEN);

    if (fs_read_file(vi_fname, buf, &size) < 0) {
        num_lines = 1;
        lines[0][0] = '\0';
        free(buf);
        return;
    }

    int col = 0;
    for (size_t i = 0; i < size && num_lines < VI_MAX_LINES; i++) {
        if (buf[i] == '\n' || col >= VI_LINE_LEN - 1) {
            lines[num_lines][col] = '\0';
            num_lines++;
            col = 0;
        } else {
            lines[num_lines][col++] = (char)buf[i];
        }
    }
    if (col > 0 || num_lines == 0) {
        lines[num_lines][col] = '\0';
        num_lines++;
    }
    if (num_lines == 0) {
        num_lines = 1;
        lines[0][0] = '\0';
    }
    free(buf);
}

static int vi_save(void) {
    uint8_t* buf = (uint8_t*)malloc(MAX_FILE_SIZE);
    if (!buf) return -1;
    size_t pos = 0;

    for (int i = 0; i < num_lines; i++) {
        int len = (int)strlen(lines[i]);
        if (pos + (size_t)len + 1 > MAX_FILE_SIZE) { free(buf); return -1; }
        memcpy(buf + pos, lines[i], (size_t)len);
        pos += (size_t)len;
        if (i < num_lines - 1) buf[pos++] = '\n';
    }

    if (fs_write_file(vi_fname, buf, pos) == 0) {
        modified = 0;
        free(buf);
        return 0;
    }
    if (fs_create_file(vi_fname, 0) < 0) { free(buf); return -1; }
    if (fs_write_file(vi_fname, buf, pos) < 0) { free(buf); return -1; }
    modified = 0;
    free(buf);
    return 0;
}

static void vi_insert_char(char c) {
    int len = (int)strlen(lines[cy]);
    if (len >= VI_LINE_LEN - 1) return;
    memmove(&lines[cy][cx + 1], &lines[cy][cx], (size_t)(len - cx + 1));
    lines[cy][cx] = c;
    cx++;
    modified = 1;
}

static void vi_delete_char_at(int row, int col) {
    int len = (int)strlen(lines[row]);
    if (col >= len) return;
    memmove(&lines[row][col], &lines[row][col + 1], (size_t)(len - col));
    modified = 1;
}

static void vi_split_line(void) {
    if (num_lines >= VI_MAX_LINES) return;
    for (int i = num_lines; i > cy + 1; i--)
        memcpy(lines[i], lines[i - 1], VI_LINE_LEN);
    num_lines++;

    int len = (int)strlen(lines[cy]);
    memcpy(lines[cy + 1], &lines[cy][cx], (size_t)(len - cx + 1));
    lines[cy][cx] = '\0';

    cy++;
    cx = 0;
    modified = 1;
}

static void vi_join_line_up(void) {
    if (cy == 0) return;
    int prev_len = (int)strlen(lines[cy - 1]);
    int cur_len  = (int)strlen(lines[cy]);
    if (prev_len + cur_len >= VI_LINE_LEN) return;

    memcpy(&lines[cy - 1][prev_len], lines[cy], (size_t)(cur_len + 1));

    for (int i = cy; i < num_lines - 1; i++)
        memcpy(lines[i], lines[i + 1], VI_LINE_LEN);
    num_lines--;

    cy--;
    cx = prev_len;
    modified = 1;
}

static void vi_delete_line(void) {
    if (num_lines <= 1) {
        lines[0][0] = '\0';
        cx = 0;
        modified = 1;
        return;
    }
    for (int i = cy; i < num_lines - 1; i++)
        memcpy(lines[i], lines[i + 1], VI_LINE_LEN);
    num_lines--;
    if (cy >= num_lines) cy = num_lines - 1;
    modified = 1;
}

static void vi_open_line_below(void) {
    if (num_lines >= VI_MAX_LINES) return;
    for (int i = num_lines; i > cy + 1; i--)
        memcpy(lines[i], lines[i - 1], VI_LINE_LEN);
    num_lines++;
    cy++;
    lines[cy][0] = '\0';
    cx = 0;
    mode = INSERT;
    modified = 1;
}

static void vi_open_line_above(void) {
    if (num_lines >= VI_MAX_LINES) return;
    for (int i = num_lines; i > cy; i--)
        memcpy(lines[i], lines[i - 1], VI_LINE_LEN);
    num_lines++;
    lines[cy][0] = '\0';
    cx = 0;
    mode = INSERT;
    modified = 1;
}

static void vi_set_msg(const char* s) {
    int i;
    for (i = 0; s[i] && i < 79; i++) msg[i] = s[i];
    msg[i] = '\0';
}

static void vi_exec_cmd(void) {
    cmd_buf[cmd_len] = '\0';

    if (strcmp(cmd_buf, "w") == 0) {
        if (vi_save() < 0)
            vi_set_msg("Error: could not save file");
        else
            vi_set_msg("File written");
    } else if (strcmp(cmd_buf, "q") == 0) {
        if (modified)
            vi_set_msg("No write since last change (use :q! to override)");
        else
            running = 0;
    } else if (strcmp(cmd_buf, "q!") == 0) {
        running = 0;
    } else if (strcmp(cmd_buf, "wq") == 0 || strcmp(cmd_buf, "x") == 0) {
        if (vi_save() < 0)
            vi_set_msg("Error: could not save file");
        else
            running = 0;
    } else {
        vi_set_msg("Unknown command");
    }

    mode = NORMAL;
    cmd_len = 0;
}

static void vi_handle_normal(char c) {
    if (c != 'd') pending_d = 0;
    if (c != 'g') pending_g = 0;

    switch (c) {
    case 'h': case KEY_LEFT:  cx--; break;
    case 'l': case KEY_RIGHT: cx++; break;
    case 'j': case KEY_DOWN:  cy++; break;
    case 'k': case KEY_UP:    cy--; break;

    case '0': cx = 0; break;
    case '$': { int len = (int)strlen(lines[cy]); cx = len > 0 ? len - 1 : 0; break; }

    case 'w': {
        int len = (int)strlen(lines[cy]);
        while (cx < len && lines[cy][cx] != ' ') cx++;
        while (cx < len && lines[cy][cx] == ' ') cx++;
        if (cx >= len && cy < num_lines - 1) { cy++; cx = 0; }
        break;
    }
    case 'b': {
        if (cx == 0 && cy > 0) { cy--; cx = (int)strlen(lines[cy]); }
        if (cx > 0) cx--;
        while (cx > 0 && lines[cy][cx] == ' ') cx--;
        while (cx > 0 && lines[cy][cx - 1] != ' ') cx--;
        break;
    }

    case 'G': cy = num_lines - 1; break;
    case 'g':
        if (pending_g) { cy = 0; cx = 0; pending_g = 0; }
        else pending_g = 1;
        break;

    case 'i': mode = INSERT; break;
    case 'a': cx++; mode = INSERT; break;
    case 'A': cx = (int)strlen(lines[cy]); mode = INSERT; break;
    case 'o': vi_open_line_below(); break;
    case 'O': vi_open_line_above(); break;

    case 'x': vi_delete_char_at(cy, cx); break;
    case 'd':
        if (pending_d) { vi_delete_line(); pending_d = 0; }
        else pending_d = 1;
        break;

    case ':': mode = COMMAND; cmd_len = 0; cmd_buf[0] = '\0'; break;

    default: break;
    }
}

static void vi_handle_insert(char c) {
    if (c == KEY_ESCAPE) {
        mode = NORMAL;
        if (cx > 0) cx--;
        return;
    }

    switch (c) {
    case '\b':
        if (cx > 0) {
            cx--;
            vi_delete_char_at(cy, cx);
        } else {
            vi_join_line_up();
        }
        break;
    case '\n':
        vi_split_line();
        break;
    case KEY_LEFT:  cx--; break;
    case KEY_RIGHT: cx++; break;
    case KEY_UP:    cy--; break;
    case KEY_DOWN:  cy++; break;
    default:
        if (c >= 0x20 && c < 0x7F)
            vi_insert_char(c);
        break;
    }
}

static void vi_handle_command(char c) {
    if (c == KEY_ESCAPE) {
        mode = NORMAL;
        cmd_len = 0;
        return;
    }
    if (c == '\n') {
        vi_exec_cmd();
        return;
    }
    if (c == '\b') {
        if (cmd_len > 0) cmd_len--;
        else { mode = NORMAL; }
        return;
    }
    if (cmd_len < 78 && c >= 0x20 && c < 0x7F) {
        cmd_buf[cmd_len++] = c;
    }
}

void vi_open(const char* filename) {
    cx = 0; cy = 0; scroll_off = 0;
    mode = NORMAL;
    cmd_len = 0;
    modified = 0;
    running = 1;
    pending_d = 0;
    pending_g = 0;
    msg[0] = '\0';

    int i;
    for (i = 0; filename[i] && i < 27; i++)
        vi_fname[i] = filename[i];
    vi_fname[i] = '\0';

    vi_load();
    vi_draw();

    while (running) {
        char c = getchar();
        msg[0] = '\0'; /* clear status message on any key */

        switch (mode) {
        case NORMAL:  vi_handle_normal(c);  break;
        case INSERT:  vi_handle_insert(c);  break;
        case COMMAND: vi_handle_command(c);  break;
        }

        vi_clamp();
        vi_scroll();
        vi_draw();
    }

    terminal_clear();
}
