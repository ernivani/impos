/* filemgr.c — Files app: browse the filesystem.
 *
 * Sidebar shows path breadcrumb, content shows directory listing.
 * Click to open directories, back button to go up.
 * Pattern follows settings.c: singleton window, per-frame tick.
 */
#include <kernel/filemgr.h>
#include <kernel/ui_window.h>
#include <kernel/gfx.h>
#include <kernel/fs.h>
#include <string.h>
#include <stdio.h>

/* ── Layout ─────────────────────────────────────────────────────── */
#define WIN_W       700
#define WIN_H       460
#define TOOLBAR_H    36
#define ROW_H        24
#define ICON_SZ       8
#define COL_BG      0xFF1E1E2E
#define COL_TOOLBAR 0xFF181825
#define COL_BORDER  0xFF313244
#define COL_TEXT    0xFFCDD6F4
#define COL_DIM     0xFF6C7086
#define COL_ACCENT  0xFF89B4FA
#define COL_DIR     0xFF89B4FA
#define COL_FILE    0xFFA6ADC8
#define COL_HOVER   0x203478F6
#define COL_SIZE    0xFF6C7086

/* ── State ──────────────────────────────────────────────────────── */
static int fm_win_id = -1;
#define FM_MAX_ENTRIES 64
static fs_dir_entry_info_t fm_entries[FM_MAX_ENTRIES];
static int fm_entry_count = 0;
static int fm_hover = -1;
static int fm_scroll = 0;
static char fm_path[256];

/* ── Helpers ────────────────────────────────────────────────────── */

static void fm_refresh(void) {
    const char *cwd = fs_get_cwd();
    strncpy(fm_path, cwd, sizeof(fm_path) - 1);
    fm_path[sizeof(fm_path) - 1] = '\0';
    fm_entry_count = fs_enumerate_directory(fm_entries, FM_MAX_ENTRIES, 0);
    fm_scroll = 0;
}

static void fm_paint(void) {
    if (fm_win_id < 0) return;
    int cw, ch;
    uint32_t *canvas = ui_window_canvas(fm_win_id, &cw, &ch);
    if (!canvas) return;

    gfx_surface_t gs = { canvas, cw, ch, cw };

    /* Background */
    gfx_surf_fill_rect(&gs, 0, 0, cw, ch, COL_BG);

    /* Toolbar */
    gfx_surf_fill_rect(&gs, 0, 0, cw, TOOLBAR_H, COL_TOOLBAR);
    gfx_surf_fill_rect(&gs, 0, TOOLBAR_H - 1, cw, 1, COL_BORDER);

    /* Back button */
    gfx_surf_draw_string_smooth(&gs, 10, (TOOLBAR_H - 16) / 2, "<", COL_ACCENT, 1);

    /* Path */
    gfx_surf_draw_string_smooth(&gs, 28, (TOOLBAR_H - 16) / 2, fm_path, COL_TEXT, 1);

    /* Column headers */
    int y = TOOLBAR_H + 4;
    gfx_surf_draw_string_smooth(&gs, 36, y, "Name", COL_DIM, 1);
    gfx_surf_draw_string_smooth(&gs, cw - 120, y, "Size", COL_DIM, 1);
    gfx_surf_draw_string_smooth(&gs, cw - 60, y, "Type", COL_DIM, 1);
    y += ROW_H;
    gfx_surf_fill_rect(&gs, 8, y - 4, cw - 16, 1, COL_BORDER);

    /* Entries */
    int max_rows = (ch - y - 4) / ROW_H;
    for (int i = fm_scroll; i < fm_entry_count && (i - fm_scroll) < max_rows; i++) {
        int ry = y + (i - fm_scroll) * ROW_H;
        fs_dir_entry_info_t *e = &fm_entries[i];

        /* Hover highlight */
        if (i == fm_hover)
            gfx_surf_fill_rect(&gs, 4, ry - 2, cw - 8, ROW_H, COL_HOVER);

        /* Icon: folder or file */
        int is_dir = (e->type == INODE_DIR);
        uint32_t icon_col = is_dir ? COL_DIR : COL_FILE;
        gfx_surf_fill_rect(&gs, 12, ry + (ROW_H - ICON_SZ) / 2 - 2,
                           ICON_SZ, ICON_SZ, icon_col);

        /* Name */
        uint32_t name_col = is_dir ? COL_DIR : COL_TEXT;
        gfx_surf_draw_string_smooth(&gs, 36, ry, e->name, name_col, 1);

        /* Size */
        if (!is_dir) {
            char sz[16];
            if (e->size < 1024)
                snprintf(sz, sizeof(sz), "%dB", (int)e->size);
            else if (e->size < 1024 * 1024)
                snprintf(sz, sizeof(sz), "%dK", (int)(e->size / 1024));
            else
                snprintf(sz, sizeof(sz), "%dM", (int)(e->size / (1024 * 1024)));
            gfx_surf_draw_string_smooth(&gs, cw - 120, ry, sz, COL_SIZE, 1);
        } else {
            gfx_surf_draw_string_smooth(&gs, cw - 120, ry, "--", COL_DIM, 1);
        }

        /* Type */
        const char *type = is_dir ? "DIR" :
                           (e->type == INODE_SYMLINK) ? "LNK" :
                           (e->type == INODE_CHARDEV) ? "DEV" : "FILE";
        gfx_surf_draw_string_smooth(&gs, cw - 60, ry, type, COL_DIM, 1);
    }

    ui_window_damage_all(fm_win_id);
}

/* ── Public API ─────────────────────────────────────────────────── */

void app_filemgr_open(void) {
    if (fm_win_id >= 0) {
        ui_window_raise(fm_win_id);
        ui_window_focus(fm_win_id);
        return;
    }

    int sw = (int)gfx_width(), sh = (int)gfx_height();
    fm_win_id = ui_window_create((sw - WIN_W) / 2, (sh - WIN_H) / 2,
                                  WIN_W, WIN_H, "Files");
    fm_hover = -1;
    fm_refresh();
    fm_paint();
}

int filemgr_tick(int mx, int my, int btn_down, int btn_up) {
    if (fm_win_id < 0) return 0;

    if (ui_window_close_requested(fm_win_id)) {
        ui_window_close_clear(fm_win_id);
        ui_window_close_animated(fm_win_id);
        fm_win_id = -1;
        return 0;
    }

    ui_win_info_t info = ui_window_info(fm_win_id);
    if (info.w <= 0) return 0;

    int lx = mx - info.cx;
    int ly = my - info.cy;

    if (lx < 0 || ly < 0 || lx >= info.cw || ly >= info.ch)
        return 0;

    /* Back button click */
    if (btn_up && ly < TOOLBAR_H && lx < 24) {
        fs_change_directory("..");
        fm_refresh();
        fm_paint();
        return 1;
    }

    /* Entry list hover and click */
    int list_y = TOOLBAR_H + 4 + ROW_H; /* after header row */
    int new_hover = -1;
    if (ly >= list_y) {
        int idx = fm_scroll + (ly - list_y) / ROW_H;
        if (idx >= 0 && idx < fm_entry_count)
            new_hover = idx;
    }

    if (new_hover != fm_hover) {
        fm_hover = new_hover;
        fm_paint();
    }

    if (btn_up && fm_hover >= 0) {
        fs_dir_entry_info_t *e = &fm_entries[fm_hover];
        if (e->type == INODE_DIR) {
            fs_change_directory(e->name);
            fm_refresh();
            fm_paint();
        }
        return 1;
    }

    if (btn_down) {
        if (ui_window_topmost_at(mx, my) != fm_win_id)
            return 0;
        ui_window_focus(fm_win_id);
        ui_window_raise(fm_win_id);
        return 1;
    }

    return 0;
}

int filemgr_win_open(void) { return fm_win_id >= 0; }

/* Legacy stubs */
void app_filemgr(void) { app_filemgr_open(); }
ui_window_t *app_filemgr_create(void) { return 0; }
void app_filemgr_on_event(ui_window_t *w, ui_event_t *e) { (void)w; (void)e; }
void app_filemgr_on_close(ui_window_t *w) { (void)w; }
