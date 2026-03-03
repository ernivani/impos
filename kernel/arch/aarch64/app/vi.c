/* vi.c — aarch64 serial stub
 *
 * The full vi editor uses direct VGA memory (0xB8000) and x86 port I/O
 * (0x3D4/0x3D5) which don't exist on aarch64. A proper serial-mode vi
 * would need VT100 escape sequences. For now, provide a minimal stub
 * that reads/edits with a line editor.
 */
#include <kernel/vi.h>
#include <kernel/fs.h>
#include <kernel/tty.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define VI_MAX_LINES 512
#define VI_LINE_LEN  256

static char lines[VI_MAX_LINES][VI_LINE_LEN];
static int num_lines;
static int modified;
static char vi_fname[28];

static void vi_load(const char *filename) {
    uint32_t parent;
    char name_buf[28];
    int ino = fs_resolve_path(filename, &parent, name_buf);
    if (ino < 0) {
        num_lines = 1;
        lines[0][0] = '\0';
        return;
    }
    inode_t node;
    fs_read_inode(ino, &node);
    if (node.type != 1 || node.size == 0) {
        num_lines = 1;
        lines[0][0] = '\0';
        return;
    }
    uint8_t *buf = (uint8_t *)malloc(node.size);
    if (!buf) { num_lines = 1; lines[0][0] = '\0'; return; }

    uint32_t offset = 0;
    while (offset < node.size) {
        int n = fs_read_at(ino, buf + offset, offset, node.size - offset);
        if (n <= 0) { free(buf); num_lines = 1; lines[0][0] = '\0'; return; }
        offset += n;
    }

    num_lines = 0;
    int col = 0;
    for (uint32_t i = 0; i < node.size && num_lines < VI_MAX_LINES; i++) {
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
    if (num_lines == 0) { num_lines = 1; lines[0][0] = '\0'; }
    free(buf);
}

static int vi_save(void) {
    size_t total = 0;
    for (int i = 0; i < num_lines; i++) {
        total += strlen(lines[i]);
        if (i < num_lines - 1) total++;
    }
    if (total == 0) total = 1;

    uint8_t *buf = (uint8_t *)malloc(total);
    if (!buf) return -1;
    size_t pos = 0;
    for (int i = 0; i < num_lines; i++) {
        int len = (int)strlen(lines[i]);
        memcpy(buf + pos, lines[i], (size_t)len);
        pos += (size_t)len;
        if (i < num_lines - 1) buf[pos++] = '\n';
    }

    if (fs_write_file(vi_fname, buf, pos) == 0) { modified = 0; free(buf); return 0; }
    if (fs_create_file(vi_fname, 0) < 0) { free(buf); return -1; }
    if (fs_write_file(vi_fname, buf, pos) < 0) { free(buf); return -1; }
    modified = 0;
    free(buf);
    return 0;
}

void vi_open(const char *filename) {
    int i;
    for (i = 0; filename[i] && i < 27; i++)
        vi_fname[i] = filename[i];
    vi_fname[i] = '\0';

    modified = 0;
    vi_load(filename);

    printf("--- vi: %s (%d lines) ---\n", vi_fname, num_lines);
    printf("Commands: :N (goto line), :p (print), :i N (insert before line N),\n");
    printf("          :d N (delete line N), :s N TEXT (set line N),\n");
    printf("          :w (save), :q (quit), :wq (save & quit)\n\n");

    char cmd[256];
    int running = 1;
    while (running) {
        printf("vi> ");
        int ci = 0;
        while (ci < 255) {
            char c = getchar();
            if (c == '\n') break;
            if (c == '\b' && ci > 0) { ci--; continue; }
            if (c >= 0x20 && c < 0x7F)
                cmd[ci++] = c;
        }
        cmd[ci] = '\0';

        if (cmd[0] == ':') {
            const char *arg = cmd + 1;
            if (strcmp(arg, "q") == 0) {
                if (modified) printf("Unsaved changes. Use :q! to force quit.\n");
                else running = 0;
            } else if (strcmp(arg, "q!") == 0) {
                running = 0;
            } else if (strcmp(arg, "w") == 0) {
                if (vi_save() == 0) printf("Written %s\n", vi_fname);
                else printf("Error saving\n");
            } else if (strcmp(arg, "wq") == 0) {
                if (vi_save() == 0) running = 0;
                else printf("Error saving\n");
            } else if (arg[0] == 'p') {
                /* Print all or range */
                for (int l = 0; l < num_lines; l++)
                    printf("%4d  %s\n", l + 1, lines[l]);
            } else if (arg[0] == 'i' && arg[1] == ' ') {
                int ln = atoi(arg + 2) - 1;
                if (ln < 0 || ln > num_lines || num_lines >= VI_MAX_LINES) {
                    printf("Invalid line\n");
                } else {
                    for (int l = num_lines; l > ln; l--)
                        memcpy(lines[l], lines[l - 1], VI_LINE_LEN);
                    lines[ln][0] = '\0';
                    num_lines++;
                    modified = 1;
                    printf("Inserted blank line at %d\n", ln + 1);
                }
            } else if (arg[0] == 'd' && arg[1] == ' ') {
                int ln = atoi(arg + 2) - 1;
                if (ln < 0 || ln >= num_lines) {
                    printf("Invalid line\n");
                } else {
                    for (int l = ln; l < num_lines - 1; l++)
                        memcpy(lines[l], lines[l + 1], VI_LINE_LEN);
                    num_lines--;
                    if (num_lines == 0) { num_lines = 1; lines[0][0] = '\0'; }
                    modified = 1;
                    printf("Deleted line %d\n", ln + 1);
                }
            } else if (arg[0] == 's' && arg[1] == ' ') {
                /* :s N TEXT — set line N to TEXT */
                int ln = 0;
                const char *p = arg + 2;
                while (*p >= '0' && *p <= '9') ln = ln * 10 + (*p++ - '0');
                ln--;
                if (*p == ' ') p++;
                if (ln < 0 || ln >= num_lines) {
                    printf("Invalid line\n");
                } else {
                    strncpy(lines[ln], p, VI_LINE_LEN - 1);
                    lines[ln][VI_LINE_LEN - 1] = '\0';
                    modified = 1;
                }
            } else {
                /* Try as line number */
                int ln = atoi(arg);
                if (ln >= 1 && ln <= num_lines)
                    printf("%4d  %s\n", ln, lines[ln - 1]);
                else
                    printf("Unknown command\n");
            }
        } else if (cmd[0]) {
            printf("Commands start with ':'\n");
        }
    }

    terminal_clear();
}
