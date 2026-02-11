#include <kernel/monitor_app.h>
#include <kernel/gfx.h>
#include <kernel/wm.h>
#include <kernel/desktop.h>
#include <kernel/ui_widget.h>
#include <kernel/ui_theme.h>
#include <kernel/ui_event.h>
#include <kernel/mouse.h>
#include <kernel/fs.h>
#include <kernel/idt.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ═══ Editor data ═════════════════════════════════════════════ */

#define ED_MAX_LINES   512
#define ED_LINE_LEN    256
#define ED_GUTTER_W    40    /* line number gutter width in pixels */
#define ED_TOOLBAR_H   32
#define ED_STATUS_H    20

static char ed_lines[ED_MAX_LINES][ED_LINE_LEN];
static int ed_line_count;
static int ed_cx, ed_cy;        /* cursor column, row */
static int ed_sx, ed_sy;        /* scroll x, y */
static char ed_filename[128];
static int ed_modified;
static char ed_status[128];
static int ed_status_input;      /* 1 = typing filename in status */
static char ed_input_buf[128];
static int ed_input_len;
static int ed_input_mode;        /* 0=none, 1=open, 2=save-as */

/* Widget indices */
static int w_btn_new, w_btn_open, w_btn_save;
static int w_filename_label, w_status_label;
static int w_text_area;

/* ═══ Buffer operations ═══════════════════════════════════════ */

static void ed_clear(void) {
    ed_line_count = 1;
    memset(ed_lines[0], 0, ED_LINE_LEN);
    ed_cx = ed_cy = 0;
    ed_sx = ed_sy = 0;
    ed_modified = 0;
    ed_filename[0] = '\0';
    ed_status[0] = '\0';
    ed_status_input = 0;
    ed_input_mode = 0;
}

static void ed_load_file(const char *name) {
    uint8_t buf[70 * 1024]; /* MAX_FILE_SIZE ~69KB */
    size_t sz = 0;
    if (fs_read_file(name, buf, &sz) != 0) {
        snprintf(ed_status, sizeof(ed_status), "Error: cannot open '%s'", name);
        return;
    }

    ed_clear();
    strncpy(ed_filename, name, sizeof(ed_filename) - 1);

    /* Parse into lines */
    int line = 0, col = 0;
    for (size_t i = 0; i < sz && line < ED_MAX_LINES; i++) {
        if (buf[i] == '\n') {
            ed_lines[line][col] = '\0';
            line++;
            col = 0;
            if (line < ED_MAX_LINES)
                ed_lines[line][0] = '\0';
        } else if (col < ED_LINE_LEN - 1) {
            ed_lines[line][col++] = buf[i];
            ed_lines[line][col] = '\0';
        }
    }
    ed_line_count = line + 1;
    if (ed_line_count < 1) ed_line_count = 1;

    ed_cx = ed_cy = 0;
    ed_sx = ed_sy = 0;
    ed_modified = 0;
    snprintf(ed_status, sizeof(ed_status), "Opened %s (%d lines)", name, ed_line_count);
}

static void ed_save_file(void) {
    if (ed_filename[0] == '\0') {
        snprintf(ed_status, sizeof(ed_status), "No filename — use Open or Save As");
        return;
    }

    /* Serialize lines into buffer */
    uint8_t buf[70 * 1024];
    size_t off = 0;
    for (int i = 0; i < ed_line_count; i++) {
        int len = (int)strlen(ed_lines[i]);
        if (off + len + 1 > sizeof(buf)) break;
        memcpy(buf + off, ed_lines[i], len);
        off += len;
        if (i < ed_line_count - 1) {
            buf[off++] = '\n';
        }
    }

    /* Ensure file exists, then write */
    fs_create_file(ed_filename, 0); /* ignore error if already exists */
    if (fs_write_file(ed_filename, buf, off) != 0) {
        snprintf(ed_status, sizeof(ed_status), "Error: cannot save '%s'", ed_filename);
        return;
    }

    ed_modified = 0;
    snprintf(ed_status, sizeof(ed_status), "Saved %s (%d bytes)", ed_filename, (int)off);
}

/* ═══ Custom draw: text area ══════════════════════════════════ */

static void ed_draw_text(ui_window_t *win, int widget_idx,
                         uint32_t *canvas, int cw, int ch) {
    ui_widget_t *wg = ui_get_widget(win, widget_idx);
    if (!wg) return;
    int x0 = wg->x, y0 = wg->y;
    int w = wg->w, h = wg->h;

    /* Clear */
    uint32_t bg = GFX_RGB(22, 22, 32);
    gfx_buf_fill_rect(canvas, cw, ch, x0, y0, w, h, bg);

    /* Gutter background */
    gfx_buf_fill_rect(canvas, cw, ch, x0, y0, ED_GUTTER_W, h, GFX_RGB(28, 28, 38));
    gfx_buf_fill_rect(canvas, cw, ch, x0 + ED_GUTTER_W, y0, 1, h, GFX_RGB(45, 45, 55));

    int vis_lines = h / FONT_H;
    int text_x = x0 + ED_GUTTER_W + 4;

    for (int i = 0; i < vis_lines; i++) {
        int line = ed_sy + i;
        if (line >= ed_line_count) break;

        int ly = y0 + i * FONT_H;

        /* Current line highlight */
        if (line == ed_cy) {
            gfx_buf_fill_rect(canvas, cw, ch, x0 + ED_GUTTER_W + 1, ly,
                               w - ED_GUTTER_W - 1, FONT_H, GFX_RGB(30, 30, 44));
        }

        /* Line number */
        char numstr[8];
        snprintf(numstr, sizeof(numstr), "%3d", line + 1);
        uint32_t num_col = (line == ed_cy) ? GFX_RGB(140, 140, 160) : GFX_RGB(70, 70, 90);
        gfx_buf_draw_string(canvas, cw, ch, x0 + 4, ly, numstr, num_col, 0);

        /* Text */
        const char *ln = ed_lines[line];
        int len = (int)strlen(ln);
        int max_chars = (w - ED_GUTTER_W - 8) / FONT_W;
        for (int c = ed_sx; c < len && (c - ed_sx) < max_chars; c++) {
            gfx_buf_draw_char(canvas, cw, ch,
                               text_x + (c - ed_sx) * FONT_W, ly,
                               ln[c], GFX_RGB(200, 200, 220), 0);
        }

        /* Cursor */
        if (line == ed_cy) {
            int cursor_x = text_x + (ed_cx - ed_sx) * FONT_W;
            if (cursor_x >= text_x && cursor_x < x0 + w - 2) {
                gfx_buf_fill_rect(canvas, cw, ch, cursor_x, ly, 2, FONT_H,
                                   GFX_RGB(200, 200, 255));
            }
        }
    }

    /* Status bar at bottom */
    int sy = y0 + h - ED_STATUS_H;
    gfx_buf_fill_rect(canvas, cw, ch, x0, sy, w, ED_STATUS_H, GFX_RGB(35, 35, 48));
    gfx_buf_fill_rect(canvas, cw, ch, x0, sy, w, 1, GFX_RGB(50, 50, 65));

    if (ed_status_input) {
        /* Show input prompt */
        const char *prompt = (ed_input_mode == 1) ? "Open: " : "Save as: ";
        char display[200];
        snprintf(display, sizeof(display), "%s%s_", prompt, ed_input_buf);
        gfx_buf_draw_string(canvas, cw, ch, x0 + 8, sy + 3,
                             display, GFX_RGB(255, 200, 80), GFX_RGB(35, 35, 48));
    } else {
        char statline[200];
        snprintf(statline, sizeof(statline), " %s%s  Ln %d, Col %d  %s",
                 ed_filename[0] ? ed_filename : "[untitled]",
                 ed_modified ? " *" : "",
                 ed_cy + 1, ed_cx + 1,
                 ed_status);
        gfx_buf_draw_string(canvas, cw, ch, x0 + 4, sy + 3,
                             statline, GFX_RGB(150, 150, 170), GFX_RGB(35, 35, 48));
    }
}

/* ═══ Custom event: text area ═════════════════════════════════ */

static void ed_ensure_visible(ui_window_t *win) {
    ui_widget_t *wg = ui_get_widget(win, w_text_area);
    if (!wg) return;
    int vis_lines = (wg->h - ED_STATUS_H) / FONT_H;
    int max_chars = (wg->w - ED_GUTTER_W - 8) / FONT_W;

    if (ed_cy < ed_sy) ed_sy = ed_cy;
    if (ed_cy >= ed_sy + vis_lines) ed_sy = ed_cy - vis_lines + 1;
    if (ed_cx < ed_sx) ed_sx = ed_cx;
    if (ed_cx >= ed_sx + max_chars) ed_sx = ed_cx - max_chars + 1;
    if (ed_sx < 0) ed_sx = 0;
    if (ed_sy < 0) ed_sy = 0;
}

static int ed_text_event(ui_window_t *win, int widget_idx, ui_event_t *ev) {
    if (ev->type == UI_EVENT_MOUSE_DOWN) {
        ui_widget_t *wg = ui_get_widget(win, widget_idx);
        if (!wg) return 0;
        int wy = ev->mouse.wy - wg->y;
        int wx = ev->mouse.wx - wg->x;
        int clicked_line = ed_sy + wy / FONT_H;
        int clicked_col = ed_sx + (wx - ED_GUTTER_W - 4) / FONT_W;
        if (clicked_line >= 0 && clicked_line < ed_line_count) {
            ed_cy = clicked_line;
            int len = (int)strlen(ed_lines[ed_cy]);
            ed_cx = (clicked_col < 0) ? 0 : (clicked_col > len ? len : clicked_col);
            return 1;
        }
        return 0;
    }

    if (ev->type != UI_EVENT_KEY_PRESS) return 0;
    char key = ev->key.key;

    /* Input mode (filename entry) */
    if (ed_status_input) {
        if (key == KEY_ESCAPE) {
            ed_status_input = 0;
            ed_input_mode = 0;
            return 1;
        }
        if (key == '\n') {
            ed_status_input = 0;
            if (ed_input_mode == 1) {
                /* Open */
                ed_load_file(ed_input_buf);
            } else if (ed_input_mode == 2) {
                /* Save as */
                strncpy(ed_filename, ed_input_buf, sizeof(ed_filename) - 1);
                ed_save_file();
            }
            ed_input_mode = 0;
            return 1;
        }
        if (key == '\b') {
            if (ed_input_len > 0) {
                ed_input_buf[--ed_input_len] = '\0';
            }
            return 1;
        }
        if (key >= 32 && key < 127 && ed_input_len < 126) {
            ed_input_buf[ed_input_len++] = key;
            ed_input_buf[ed_input_len] = '\0';
            return 1;
        }
        return 0;
    }

    /* Normal text editing */
    int len = (int)strlen(ed_lines[ed_cy]);

    switch (key) {
    case KEY_UP:
        if (ed_cy > 0) {
            ed_cy--;
            len = (int)strlen(ed_lines[ed_cy]);
            if (ed_cx > len) ed_cx = len;
        }
        ed_ensure_visible(win);
        return 1;
    case KEY_DOWN:
        if (ed_cy < ed_line_count - 1) {
            ed_cy++;
            len = (int)strlen(ed_lines[ed_cy]);
            if (ed_cx > len) ed_cx = len;
        }
        ed_ensure_visible(win);
        return 1;
    case KEY_LEFT:
        if (ed_cx > 0) ed_cx--;
        else if (ed_cy > 0) {
            ed_cy--;
            ed_cx = (int)strlen(ed_lines[ed_cy]);
        }
        ed_ensure_visible(win);
        return 1;
    case KEY_RIGHT:
        if (ed_cx < len) ed_cx++;
        else if (ed_cy < ed_line_count - 1) {
            ed_cy++;
            ed_cx = 0;
        }
        ed_ensure_visible(win);
        return 1;
    case KEY_HOME:
        ed_cx = 0;
        ed_ensure_visible(win);
        return 1;
    case KEY_END:
        ed_cx = len;
        ed_ensure_visible(win);
        return 1;
    case KEY_PGUP: {
        ui_widget_t *wg = ui_get_widget(win, w_text_area);
        int page = wg ? (wg->h - ED_STATUS_H) / FONT_H : 10;
        ed_cy -= page;
        if (ed_cy < 0) ed_cy = 0;
        len = (int)strlen(ed_lines[ed_cy]);
        if (ed_cx > len) ed_cx = len;
        ed_ensure_visible(win);
        return 1;
    }
    case KEY_PGDN: {
        ui_widget_t *wg = ui_get_widget(win, w_text_area);
        int page = wg ? (wg->h - ED_STATUS_H) / FONT_H : 10;
        ed_cy += page;
        if (ed_cy >= ed_line_count) ed_cy = ed_line_count - 1;
        len = (int)strlen(ed_lines[ed_cy]);
        if (ed_cx > len) ed_cx = len;
        ed_ensure_visible(win);
        return 1;
    }
    case '\b': /* Backspace */
        if (ed_cx > 0) {
            /* Delete char before cursor */
            memmove(&ed_lines[ed_cy][ed_cx - 1],
                    &ed_lines[ed_cy][ed_cx],
                    strlen(&ed_lines[ed_cy][ed_cx]) + 1);
            ed_cx--;
            ed_modified = 1;
        } else if (ed_cy > 0) {
            /* Join with previous line */
            int prev_len = (int)strlen(ed_lines[ed_cy - 1]);
            if (prev_len + (int)strlen(ed_lines[ed_cy]) < ED_LINE_LEN - 1) {
                strcat(ed_lines[ed_cy - 1], ed_lines[ed_cy]);
                /* Shift lines up */
                for (int i = ed_cy; i < ed_line_count - 1; i++)
                    memcpy(ed_lines[i], ed_lines[i + 1], ED_LINE_LEN);
                ed_line_count--;
                ed_cy--;
                ed_cx = prev_len;
                ed_modified = 1;
            }
        }
        ed_ensure_visible(win);
        return 1;
    case KEY_DEL:
        if (ed_cx < len) {
            memmove(&ed_lines[ed_cy][ed_cx],
                    &ed_lines[ed_cy][ed_cx + 1],
                    strlen(&ed_lines[ed_cy][ed_cx + 1]) + 1);
            ed_modified = 1;
        } else if (ed_cy < ed_line_count - 1) {
            /* Join with next line */
            if (len + (int)strlen(ed_lines[ed_cy + 1]) < ED_LINE_LEN - 1) {
                strcat(ed_lines[ed_cy], ed_lines[ed_cy + 1]);
                for (int i = ed_cy + 1; i < ed_line_count - 1; i++)
                    memcpy(ed_lines[i], ed_lines[i + 1], ED_LINE_LEN);
                ed_line_count--;
                ed_modified = 1;
            }
        }
        return 1;
    case '\n': /* Enter — split line */
        if (ed_line_count < ED_MAX_LINES) {
            /* Shift lines down */
            for (int i = ed_line_count; i > ed_cy + 1; i--)
                memcpy(ed_lines[i], ed_lines[i - 1], ED_LINE_LEN);
            ed_line_count++;
            /* Copy remainder to new line */
            strncpy(ed_lines[ed_cy + 1], &ed_lines[ed_cy][ed_cx], ED_LINE_LEN - 1);
            ed_lines[ed_cy + 1][ED_LINE_LEN - 1] = '\0';
            ed_lines[ed_cy][ed_cx] = '\0';
            ed_cy++;
            ed_cx = 0;
            ed_modified = 1;
        }
        ed_ensure_visible(win);
        return 1;
    case '\t': /* Tab = 4 spaces */
        for (int t = 0; t < 4 && len < ED_LINE_LEN - 2; t++) {
            memmove(&ed_lines[ed_cy][ed_cx + 1],
                    &ed_lines[ed_cy][ed_cx],
                    strlen(&ed_lines[ed_cy][ed_cx]) + 1);
            ed_lines[ed_cy][ed_cx] = ' ';
            ed_cx++;
            len++;
        }
        ed_modified = 1;
        ed_ensure_visible(win);
        return 1;
    default:
        break;
    }

    /* Ctrl+S = save, Ctrl+O = open, Ctrl+N = new */
    if (key == 19) { /* Ctrl+S */
        if (ed_filename[0]) {
            ed_save_file();
        } else {
            ed_status_input = 1;
            ed_input_mode = 2;
            ed_input_len = 0;
            ed_input_buf[0] = '\0';
        }
        return 1;
    }
    if (key == 15) { /* Ctrl+O */
        ed_status_input = 1;
        ed_input_mode = 1;
        ed_input_len = 0;
        ed_input_buf[0] = '\0';
        return 1;
    }
    if (key == 14) { /* Ctrl+N */
        ed_clear();
        snprintf(ed_status, sizeof(ed_status), "New file");
        return 1;
    }

    /* Printable character insertion */
    if (key >= 32 && (unsigned char)key < 127 && len < ED_LINE_LEN - 2) {
        memmove(&ed_lines[ed_cy][ed_cx + 1],
                &ed_lines[ed_cy][ed_cx],
                strlen(&ed_lines[ed_cy][ed_cx]) + 1);
        ed_lines[ed_cy][ed_cx] = key;
        ed_cx++;
        ed_modified = 1;
        ed_ensure_visible(win);
        return 1;
    }

    return 0;
}

/* ═══ Button callbacks ════════════════════════════════════════ */

static void on_new(ui_window_t *win, int idx) {
    (void)idx;
    ed_clear();
    snprintf(ed_status, sizeof(ed_status), "New file");
    win->dirty = 1;
}

static void on_open(ui_window_t *win, int idx) {
    (void)idx;
    ed_status_input = 1;
    ed_input_mode = 1;
    ed_input_len = 0;
    ed_input_buf[0] = '\0';
    win->dirty = 1;
}

static void on_save(ui_window_t *win, int idx) {
    (void)idx;
    if (ed_filename[0]) {
        ed_save_file();
    } else {
        ed_status_input = 1;
        ed_input_mode = 2;
        ed_input_len = 0;
        ed_input_buf[0] = '\0';
    }
    win->dirty = 1;
}

/* ═══ Event handler ═══════════════════════════════════════════ */

void app_editor_on_event(ui_window_t *win, ui_event_t *ev) {
    (void)win;
    (void)ev;
    /* All events handled by custom widget event callback */
}

/* ═══ Create ══════════════════════════════════════════════════ */

ui_window_t *app_editor_create(void) {
    int fb_w = (int)gfx_width(), fb_h = (int)gfx_height();
    int win_w = 900, win_h = 600;
    if (win_w > fb_w - 40) win_w = fb_w - 40;
    if (win_h > fb_h - 100) win_h = fb_h - 100;

    ed_clear();

    ui_window_t *win = ui_window_create(fb_w / 2 - win_w / 2,
                                         fb_h / 2 - win_h / 2 - 20,
                                         win_w, win_h, "Editor");
    if (!win) return 0;

    int cw, ch;
    wm_get_canvas(win->wm_id, &cw, &ch);

    /* Toolbar card */
    ui_add_card(win, 0, 0, cw, ED_TOOLBAR_H, NULL, GFX_RGB(32, 32, 42), 0);
    w_btn_new  = ui_add_button(win, 8, 4, 48, 24, "New", on_new);
    w_btn_open = ui_add_button(win, 62, 4, 52, 24, "Open", on_open);
    w_btn_save = ui_add_button(win, 120, 4, 52, 24, "Save", on_save);

    /* Separator */
    ui_add_separator(win, 0, ED_TOOLBAR_H - 1, cw);

    /* Text area (custom widget) */
    w_text_area = ui_add_custom(win, 0, ED_TOOLBAR_H, cw, ch - ED_TOOLBAR_H,
                                 ed_draw_text, ed_text_event, NULL);

    /* Auto-focus custom text area */
    win->focused_widget = w_text_area;

    return win;
}

/* ═══ Standalone entry point ══════════════════════════════════ */

void app_editor(void) {
    ui_window_t *win = app_editor_create();
    if (!win) return;
    ui_app_run(win, app_editor_on_event);
    ui_window_destroy(win);
}

/* ═══ Legacy compat: old names still compile ══════════════════ */

ui_window_t *app_monitor_create(void) { return app_editor_create(); }
void app_monitor_on_event(ui_window_t *win, ui_event_t *ev) { app_editor_on_event(win, ev); }
void app_monitor(void) { app_editor(); }
