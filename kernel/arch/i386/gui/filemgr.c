#include <kernel/filemgr.h>
#include <kernel/gfx.h>
#include <kernel/wm.h>
#include <kernel/desktop.h>
#include <kernel/ui_widget.h>
#include <kernel/ui_theme.h>
#include <kernel/ui_event.h>
#include <kernel/fs.h>
#include <kernel/mouse.h>
#include <kernel/idt.h>
#include <string.h>
#include <stdio.h>

#define FM_MAX_ENTRIES 64

static fs_dir_entry_info_t entries[FM_MAX_ENTRIES];
static int entry_count;

/* List items */
static const char *file_items[FM_MAX_ENTRIES];
static char file_bufs[FM_MAX_ENTRIES][80];
static int file_count;

static int w_path_label, w_list;

static void fm_load_dir(ui_window_t *win) {
    entry_count = fs_enumerate_directory(entries, FM_MAX_ENTRIES, 0);
    file_count = 0;

    for (int i = 0; i < entry_count && file_count < FM_MAX_ENTRIES; i++) {
        char icon;
        if (entries[i].type == INODE_DIR) icon = 'D';
        else if (entries[i].type == INODE_SYMLINK) icon = 'L';
        else icon = 'F';

        if (entries[i].type != INODE_DIR) {
            char sizebuf[16];
            uint32_t sz = entries[i].size;
            if (sz < 1024)
                snprintf(sizebuf, sizeof(sizebuf), "%dB", (int)sz);
            else
                snprintf(sizebuf, sizeof(sizebuf), "%dKB", (int)(sz / 1024));
            snprintf(file_bufs[file_count], 80, " %c  %-32s  %s",
                     icon, entries[i].name, sizebuf);
        } else {
            snprintf(file_bufs[file_count], 80, " %c  %s/",
                     icon, entries[i].name);
        }
        file_items[file_count] = file_bufs[file_count];
        file_count++;
    }

    /* Update path label */
    ui_widget_t *path = ui_get_widget(win, w_path_label);
    if (path) strncpy(path->label.text, fs_get_cwd(), UI_TEXT_MAX - 1);

    /* Update list */
    ui_widget_t *list = ui_get_widget(win, w_list);
    if (list) {
        list->list.items = file_items;
        list->list.count = file_count;
        list->list.scroll = 0;
        list->list.selected = 0;
    }
    win->dirty = 1;
}

static void on_activate(ui_window_t *win, int idx) {
    ui_widget_t *list = ui_get_widget(win, idx);
    if (!list) return;
    int sel = list->list.selected;
    if (sel < 0 || sel >= entry_count) return;

    if (entries[sel].type == INODE_DIR) {
        fs_change_directory(entries[sel].name);
        fm_load_dir(win);
    }
}

static void fm_on_event(ui_window_t *win, ui_event_t *ev) {
    if (ev->type == UI_EVENT_KEY_PRESS) {
        if (ev->key.key == '\b') {
            fs_change_directory("..");
            fm_load_dir(win);
        }
    }
}

void app_filemgr(void) {
    int fb_w = (int)gfx_width(), fb_h = (int)gfx_height();
    int win_w = fb_w - 200;
    int win_h = fb_h - TASKBAR_H - 60;

    /* Save original directory */
    char orig_cwd[512];
    strncpy(orig_cwd, fs_get_cwd(), 511);
    orig_cwd[511] = '\0';

    ui_window_t *win = ui_window_create(100, 20, win_w, win_h, "Files");
    if (!win) { fs_change_directory(orig_cwd); return; }

    int cw, ch;
    wm_get_canvas(win->wm_id, &cw, &ch);

    /* Path header */
    ui_add_panel(win, 0, 0, cw, 30, NULL);
    w_path_label = ui_add_label(win, 8, 7, cw - 16, 16, "", GFX_RGB(180, 180, 180));
    ui_add_separator(win, 0, 29, cw);

    /* Column header */
    ui_add_label(win, 30, 32, 100, 18, "Name", GFX_RGB(120, 120, 120));
    ui_add_label(win, cw - 80, 32, 60, 18, "Size", GFX_RGB(120, 120, 120));

    /* File list */
    w_list = ui_add_list(win, 0, 50, cw, ch - 50, NULL, 0);
    ui_widget_t *list = ui_get_widget(win, w_list);
    if (list) list->list.on_activate = on_activate;

    fm_load_dir(win);
    ui_app_run(win, fm_on_event);
    ui_window_destroy(win);

    /* Restore original directory */
    fs_change_directory(orig_cwd);
}
