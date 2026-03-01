/* notes.c — Notes app using widget toolkit (Phase 7.5)
 *
 * Simple text editor with Save/Clear buttons.
 * Saves to /notes.txt via fs_write_file().
 */
#include <kernel/ui_widget.h>
#include <kernel/ui_window.h>
#include <kernel/ui_theme.h>
#include <kernel/fs.h>
#include <kernel/gfx.h>
#include <string.h>
#include <stdio.h>

/* ── State ─────────────────────────────────────────────────────── */

static ui_window_t *notes_win = NULL;
static int text_idx   = -1;
static int status_idx = -1;

#define NOTES_FILE "/notes.txt"

/* ── Callbacks ─────────────────────────────────────────────────── */

static void cb_save(ui_window_t *win, int idx)
{
    (void)idx;
    ui_widget_t *w = ui_get_widget(win, text_idx);
    if (!w) return;

    int len = (int)strlen(w->textinput.text);

    /* Ensure file exists */
    uint32_t parent;
    char fname[64];
    int ino = fs_resolve_path(NOTES_FILE, &parent, fname);
    if (ino < 0)
        ino = fs_create_file(NOTES_FILE, 0 /* file, not dir */);
    if (ino < 0) {
        ui_widget_t *st = ui_get_widget(win, status_idx);
        if (st) strncpy(st->label.text, "Error: cannot create file",
                        UI_TEXT_MAX - 1);
        win->dirty = 1;
        return;
    }

    fs_truncate(NOTES_FILE, 0);
    fs_write_at((uint32_t)ino, (const uint8_t *)w->textinput.text,
                0, (uint32_t)len);

    ui_widget_t *st = ui_get_widget(win, status_idx);
    if (st) strncpy(st->label.text, "Saved", UI_TEXT_MAX - 1);
    win->dirty = 1;
}

static void cb_clear(ui_window_t *win, int idx)
{
    (void)idx;
    ui_widget_t *w = ui_get_widget(win, text_idx);
    if (!w) return;
    w->textinput.text[0] = '\0';
    w->textinput.cursor = 0;

    ui_widget_t *st = ui_get_widget(win, status_idx);
    if (st) strncpy(st->label.text, "Cleared", UI_TEXT_MAX - 1);
    win->dirty = 1;
}

static void load_file(void)
{
    if (!notes_win || text_idx < 0) return;
    ui_widget_t *w = ui_get_widget(notes_win, text_idx);
    if (!w) return;

    uint32_t parent;
    char fname[64];
    int ino = fs_resolve_path(NOTES_FILE, &parent, fname);
    if (ino < 0) return;

    inode_t node;
    if (fs_read_inode((uint32_t)ino, &node) < 0) return;

    int size = (int)node.size;
    if (size <= 0) return;
    if (size >= UI_TEXT_MAX) size = UI_TEXT_MAX - 1;

    fs_read_at((uint32_t)ino, (uint8_t *)w->textinput.text,
               0, (uint32_t)size);
    w->textinput.text[size] = '\0';
    w->textinput.cursor = size;
    notes_win->dirty = 1;
}

/* ── Public API ────────────────────────────────────────────────── */

void app_notes_open(void)
{
    if (notes_win) {
        ui_window_focus(notes_win->wm_id);
        ui_window_raise(notes_win->wm_id);
        return;
    }

    int w = 400, h = 300;
    int sw = (int)gfx_width(), sh = (int)gfx_height();
    notes_win = uw_create(sw / 2 - w / 2 + 40, sh / 2 - h / 2, w, h,
                          "Notes");
    if (!notes_win) return;

    /* Toolbar: Save, Clear, status label */
    ui_add_button(notes_win, 12, 8, 60, 28, "Save", cb_save);
    ui_add_button(notes_win, 80, 8, 60, 28, "Clear", cb_clear);
    status_idx = ui_add_label(notes_win, 150, 14, 200, 16, NOTES_FILE,
                              ui_theme.text_dim);

    /* Separator */
    ui_add_separator(notes_win, 12, 42, w - 24);

    /* Text input area */
    text_idx = ui_add_textinput(notes_win, 12, 50, w - 24, 28,
                                "Type your notes here...",
                                UI_TEXT_MAX - 1, 0);

    /* Auto-focus text input */
    notes_win->focused_widget = text_idx;

    load_file();
    uw_redraw(notes_win);
}

int notes_tick(int mx, int my, int btn_down, int btn_up)
{
    if (!notes_win) return 0;
    int r = uw_tick(notes_win, mx, my, btn_down, btn_up, 0);
    if (notes_win && notes_win->wm_id < 0) {
        notes_win = NULL;
        text_idx = -1;
        status_idx = -1;
    }
    return r;
}

int notes_win_open(void) { return notes_win != NULL; }
