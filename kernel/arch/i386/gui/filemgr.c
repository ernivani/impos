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
#include <kernel/user.h>
#include <string.h>
#include <stdio.h>

/* ═══ Layout constants ════════════════════════════════════════ */

#define FM_NAV_H        40    /* Navigation bar height */
#define FM_SIDEBAR_W   140    /* Sidebar width */
#define FM_COL_HDR_H    24    /* Column header height */
#define FM_STATUS_H     24    /* Status bar height */
#define FM_ROW_H        22    /* File list row height */

/* Column offsets relative to file list area */
#define FM_COL_ICON      8    /* Icon/type glyph */
#define FM_COL_NAME     26    /* Name text */
#define FM_COL_SIZE_OFF 80    /* Size column offset from right */
#define FM_COL_TYPE_OFF 20    /* Type column offset from right */

/* ═══ Data ════════════════════════════════════════════════════ */

#define FM_MAX_ENTRIES  64

static fs_dir_entry_info_t fm_entries[FM_MAX_ENTRIES];
static int fm_count;
static int fm_selected;
static int fm_scroll;
static int fm_sort_col;      /* 0=name, 1=size, 2=type */
static int fm_sort_asc;      /* 1=ascending, 0=descending */
static char fm_path[256];

/* Navigation history */
#define FM_HISTORY_MAX  16
static uint32_t fm_history[FM_HISTORY_MAX];
static int fm_hist_pos, fm_hist_len;

/* Saved cwd for restore on close */
static char fm_orig_cwd[512];

/* Widget indices */
static int w_nav_bar;
static int w_sidebar;
static int w_col_hdr;
static int w_filelist;
static int w_status;

/* Sidebar items */
#define FM_FAV_COUNT  4
#define FM_SYS_COUNT  1
#define FM_SIDEBAR_ITEMS (FM_FAV_COUNT + FM_SYS_COUNT)

static const char *sidebar_labels[] = { "Home", "Desktop", "Documents", "Trash", "/" };
static int sidebar_hover = -1;

/* Status string */
static char fm_status_str[64];

/* ═══ Helpers ═════════════════════════════════════════════════ */

static void fm_update_path(void) {
    strncpy(fm_path, fs_get_cwd(), sizeof(fm_path) - 1);
    fm_path[sizeof(fm_path) - 1] = '\0';
}

static void fm_history_push(void) {
    uint32_t ino = fs_get_cwd_inode();
    if (fm_hist_pos > 0 && fm_history[fm_hist_pos - 1] == ino)
        return; /* don't duplicate */
    if (fm_hist_pos < FM_HISTORY_MAX) {
        fm_history[fm_hist_pos] = ino;
        fm_hist_pos++;
        fm_hist_len = fm_hist_pos;
    }
}

static int fm_name_cmp(const fs_dir_entry_info_t *a, const fs_dir_entry_info_t *b) {
    /* Directories always before files */
    if (a->type == INODE_DIR && b->type != INODE_DIR) return -1;
    if (a->type != INODE_DIR && b->type == INODE_DIR) return 1;
    return strcmp(a->name, b->name);
}

static int fm_size_cmp(const fs_dir_entry_info_t *a, const fs_dir_entry_info_t *b) {
    if (a->type == INODE_DIR && b->type != INODE_DIR) return -1;
    if (a->type != INODE_DIR && b->type == INODE_DIR) return 1;
    if (a->size < b->size) return -1;
    if (a->size > b->size) return 1;
    return 0;
}

static int fm_type_cmp(const fs_dir_entry_info_t *a, const fs_dir_entry_info_t *b) {
    if (a->type < b->type) return -1;
    if (a->type > b->type) return 1;
    return strcmp(a->name, b->name);
}

static void fm_sort(void) {
    for (int i = 0; i < fm_count - 1; i++) {
        for (int j = i + 1; j < fm_count; j++) {
            int cmp = 0;
            switch (fm_sort_col) {
            case 0: cmp = fm_name_cmp(&fm_entries[i], &fm_entries[j]); break;
            case 1: cmp = fm_size_cmp(&fm_entries[i], &fm_entries[j]); break;
            case 2: cmp = fm_type_cmp(&fm_entries[i], &fm_entries[j]); break;
            }
            if (!fm_sort_asc) cmp = -cmp;
            if (cmp > 0) {
                fs_dir_entry_info_t tmp = fm_entries[i];
                fm_entries[i] = fm_entries[j];
                fm_entries[j] = tmp;
            }
        }
    }
}

static void fm_load_dir(ui_window_t *win) {
    fm_count = fs_enumerate_directory(fm_entries, FM_MAX_ENTRIES, 0);
    fm_sort();
    fm_selected = 0;
    fm_scroll = 0;
    fm_update_path();

    snprintf(fm_status_str, sizeof(fm_status_str), "%d items", fm_count);
    win->dirty = 1;
}

static void fm_navigate_to_inode(ui_window_t *win, uint32_t inode) {
    fm_history_push();
    fs_change_directory_by_inode(inode);
    fm_load_dir(win);
}

static void fm_navigate_up(ui_window_t *win) {
    fm_history_push();
    fs_change_directory("..");
    fm_load_dir(win);
}

static void fm_navigate_to_path(ui_window_t *win, const char *path) {
    fm_history_push();
    fs_change_directory(path);
    fm_load_dir(win);
}

static void fm_go_back(ui_window_t *win) {
    if (fm_hist_pos <= 1) return;
    fm_hist_pos--;
    fs_change_directory_by_inode(fm_history[fm_hist_pos - 1]);
    fm_load_dir(win);
}

static void fm_go_forward(ui_window_t *win) {
    if (fm_hist_pos >= fm_hist_len) return;
    fs_change_directory_by_inode(fm_history[fm_hist_pos]);
    fm_hist_pos++;
    fm_load_dir(win);
}

static void fm_open_selected(ui_window_t *win) {
    if (fm_selected < 0 || fm_selected >= fm_count) return;
    fs_dir_entry_info_t *e = &fm_entries[fm_selected];
    if (e->type == INODE_DIR) {
        fm_navigate_to_inode(win, e->inode);
    }
    /* For files: could open editor in future */
}

static void fm_delete_selected(ui_window_t *win) {
    if (fm_selected < 0 || fm_selected >= fm_count) return;
    fs_dir_entry_info_t *e = &fm_entries[fm_selected];
    fs_delete_file(e->name);
    fm_load_dir(win);
}

/* Get sidebar path for given index */
static const char *fm_sidebar_path(int idx) {
    static char pathbuf[128];
    const char *user = user_get_current();
    if (!user) user = "root";

    switch (idx) {
    case 0: /* Home */
        snprintf(pathbuf, sizeof(pathbuf), "/home/%s", user);
        return pathbuf;
    case 1: /* Desktop */
        snprintf(pathbuf, sizeof(pathbuf), "/home/%s/Desktop", user);
        return pathbuf;
    case 2: /* Documents */
        snprintf(pathbuf, sizeof(pathbuf), "/home/%s/Documents", user);
        return pathbuf;
    case 3: /* Trash */
        snprintf(pathbuf, sizeof(pathbuf), "/home/%s/Trash", user);
        return pathbuf;
    case 4: /* / */
        return "/";
    }
    return "/";
}

/* ═══ Custom draw: navigation bar ═════════════════════════════ */

static void fm_draw_nav(ui_window_t *win, int widget_idx,
                         uint32_t *canvas, int cw, int ch) {
    ui_widget_t *wg = ui_get_widget(win, widget_idx);
    if (!wg) return;
    int x0 = wg->x, y0 = wg->y, w = wg->w, h = wg->h;

    /* Background */
    gfx_buf_fill_rect(canvas, cw, ch, x0, y0, w, h, ui_theme.surface);

    /* Back button */
    int bx = x0 + 8, by = y0 + 8, bw = 50, bh = 24;
    uint32_t back_bg = (fm_hist_pos > 1) ? ui_theme.btn_bg : ui_theme.surface;
    uint32_t back_fg = (fm_hist_pos > 1) ? ui_theme.text_primary : ui_theme.text_dim;
    gfx_buf_fill_rect(canvas, cw, ch, bx, by, bw, bh, back_bg);
    gfx_buf_draw_string(canvas, cw, ch, bx + 8, by + 4, "<Back", back_fg, back_bg);

    /* Forward button */
    bx += bw + 4;
    uint32_t fwd_bg = (fm_hist_pos < fm_hist_len) ? ui_theme.btn_bg : ui_theme.surface;
    uint32_t fwd_fg = (fm_hist_pos < fm_hist_len) ? ui_theme.text_primary : ui_theme.text_dim;
    gfx_buf_fill_rect(canvas, cw, ch, bx, by, bw, bh, fwd_bg);
    gfx_buf_draw_string(canvas, cw, ch, bx + 8, by + 4, "Fwd>", fwd_fg, fwd_bg);

    /* Up button */
    bx += bw + 4;
    int up_w = 30;
    gfx_buf_fill_rect(canvas, cw, ch, bx, by, up_w, bh, ui_theme.btn_bg);
    gfx_buf_draw_string(canvas, cw, ch, bx + 8, by + 4, "Up", ui_theme.text_primary, ui_theme.btn_bg);

    /* Path display */
    int path_x = bx + up_w + 12;
    int path_w = w - (path_x - x0) - 8;
    if (path_w > 0) {
        gfx_buf_fill_rect(canvas, cw, ch, path_x, by, path_w, bh, ui_theme.input_bg);
        gfx_buf_draw_rect(canvas, cw, ch, path_x, by, path_w, bh, ui_theme.input_border);
        /* Truncate path to fit */
        int max_chars = (path_w - 12) / FONT_W;
        if (max_chars > 0) {
            char display[256];
            int len = (int)strlen(fm_path);
            if (len > max_chars) {
                snprintf(display, sizeof(display), "...%s", fm_path + len - max_chars + 3);
            } else {
                strncpy(display, fm_path, sizeof(display) - 1);
                display[sizeof(display) - 1] = '\0';
            }
            gfx_buf_draw_string(canvas, cw, ch, path_x + 6, by + 4,
                                 display, ui_theme.text_primary, ui_theme.input_bg);
        }
    }

    /* Bottom border */
    gfx_buf_fill_rect(canvas, cw, ch, x0, y0 + h - 1, w, 1, ui_theme.border);
}

static int fm_nav_event(ui_window_t *win, int widget_idx, ui_event_t *ev) {
    if (ev->type != UI_EVENT_MOUSE_DOWN) return 0;
    ui_widget_t *wg = ui_get_widget(win, widget_idx);
    if (!wg) return 0;

    int wx = ev->mouse.wx;
    int wy = ev->mouse.wy;
    int by = wg->y + 8, bh = 24;

    if (wy < by || wy >= by + bh) return 0;

    int bx = wg->x + 8;
    /* Back */
    if (wx >= bx && wx < bx + 50) { fm_go_back(win); return 1; }
    bx += 54;
    /* Forward */
    if (wx >= bx && wx < bx + 50) { fm_go_forward(win); return 1; }
    bx += 54;
    /* Up */
    if (wx >= bx && wx < bx + 30) { fm_navigate_up(win); return 1; }

    return 0;
}

/* ═══ Custom draw: sidebar ════════════════════════════════════ */

static void fm_draw_sidebar(ui_window_t *win, int widget_idx,
                              uint32_t *canvas, int cw, int ch) {
    ui_widget_t *wg = ui_get_widget(win, widget_idx);
    if (!wg) return;
    int x0 = wg->x, y0 = wg->y, w = wg->w, h = wg->h;

    /* Background */
    gfx_buf_fill_rect(canvas, cw, ch, x0, y0, w, h, GFX_RGB(22, 22, 36));

    /* Right border */
    gfx_buf_fill_rect(canvas, cw, ch, x0 + w - 1, y0, 1, h, ui_theme.border);

    int ty = y0 + 8;
    int pad = 10;

    /* Favorites header */
    gfx_buf_draw_string(canvas, cw, ch, x0 + pad, ty,
                         "Favorites", ui_theme.text_dim, GFX_RGB(22, 22, 36));
    ty += 20;

    /* Favorites items */
    for (int i = 0; i < FM_FAV_COUNT; i++) {
        int row_y = ty + i * FM_ROW_H;
        int hovered = (sidebar_hover == i);
        uint32_t row_bg = hovered ? ui_theme.list_sel_bg : GFX_RGB(22, 22, 36);
        gfx_buf_fill_rect(canvas, cw, ch, x0, row_y, w - 1, FM_ROW_H, row_bg);

        /* Accent bar on hover */
        if (hovered)
            gfx_buf_fill_rect(canvas, cw, ch, x0 + 2, row_y + 3, 3, FM_ROW_H - 6,
                               ui_theme.accent);

        /* Folder icon glyph */
        uint32_t icon_color;
        switch (i) {
        case 0: icon_color = GFX_RGB(100, 180, 255); break; /* Home: blue */
        case 1: icon_color = GFX_RGB(200, 160, 80); break;  /* Desktop: gold */
        case 2: icon_color = GFX_RGB(100, 180, 255); break; /* Documents: blue */
        case 3: icon_color = GFX_RGB(160, 160, 160); break; /* Trash: gray */
        default: icon_color = ui_theme.icon; break;
        }
        gfx_buf_draw_char(canvas, cw, ch, x0 + pad, row_y + (FM_ROW_H - FONT_H) / 2,
                           (i == 3) ? 'X' : 'D', icon_color, row_bg);

        gfx_buf_draw_string(canvas, cw, ch, x0 + pad + FONT_W + 6,
                             row_y + (FM_ROW_H - FONT_H) / 2,
                             sidebar_labels[i], ui_theme.text_primary, row_bg);
    }

    ty += FM_FAV_COUNT * FM_ROW_H + 12;

    /* System header */
    gfx_buf_draw_string(canvas, cw, ch, x0 + pad, ty,
                         "System", ui_theme.text_dim, GFX_RGB(22, 22, 36));
    ty += 20;

    /* System items */
    for (int i = 0; i < FM_SYS_COUNT; i++) {
        int idx = FM_FAV_COUNT + i;
        int row_y = ty + i * FM_ROW_H;
        int hovered = (sidebar_hover == idx);
        uint32_t row_bg = hovered ? ui_theme.list_sel_bg : GFX_RGB(22, 22, 36);
        gfx_buf_fill_rect(canvas, cw, ch, x0, row_y, w - 1, FM_ROW_H, row_bg);

        if (hovered)
            gfx_buf_fill_rect(canvas, cw, ch, x0 + 2, row_y + 3, 3, FM_ROW_H - 6,
                               ui_theme.accent);

        gfx_buf_draw_char(canvas, cw, ch, x0 + pad, row_y + (FM_ROW_H - FONT_H) / 2,
                           '/', GFX_RGB(200, 160, 80), row_bg);
        gfx_buf_draw_string(canvas, cw, ch, x0 + pad + FONT_W + 6,
                             row_y + (FM_ROW_H - FONT_H) / 2,
                             sidebar_labels[idx], ui_theme.text_primary, row_bg);
    }
}

static int fm_sidebar_event(ui_window_t *win, int widget_idx, ui_event_t *ev) {
    ui_widget_t *wg = ui_get_widget(win, widget_idx);
    if (!wg) return 0;

    if (ev->type == UI_EVENT_MOUSE_DOWN) {
        int wy = ev->mouse.wy - wg->y;

        /* Favorites area starts at y=28 */
        int fav_start = 28;
        int fav_end = fav_start + FM_FAV_COUNT * FM_ROW_H;

        if (wy >= fav_start && wy < fav_end) {
            int idx = (wy - fav_start) / FM_ROW_H;
            if (idx >= 0 && idx < FM_FAV_COUNT) {
                fm_navigate_to_path(win, fm_sidebar_path(idx));
                return 1;
            }
        }

        /* System area starts after favorites + 32px gap */
        int sys_start = fav_end + 32;
        int sys_end = sys_start + FM_SYS_COUNT * FM_ROW_H;

        if (wy >= sys_start && wy < sys_end) {
            int idx = FM_FAV_COUNT + (wy - sys_start) / FM_ROW_H;
            if (idx >= FM_FAV_COUNT && idx < FM_SIDEBAR_ITEMS) {
                fm_navigate_to_path(win, fm_sidebar_path(idx));
                return 1;
            }
        }
    }

    if (ev->type == UI_EVENT_MOUSE_MOVE) {
        int wy = ev->mouse.wy - wg->y;
        int old_hover = sidebar_hover;
        sidebar_hover = -1;

        int fav_start = 28;
        int fav_end = fav_start + FM_FAV_COUNT * FM_ROW_H;
        if (wy >= fav_start && wy < fav_end) {
            sidebar_hover = (wy - fav_start) / FM_ROW_H;
        }

        int sys_start = fav_end + 32;
        int sys_end = sys_start + FM_SYS_COUNT * FM_ROW_H;
        if (wy >= sys_start && wy < sys_end) {
            sidebar_hover = FM_FAV_COUNT + (wy - sys_start) / FM_ROW_H;
        }

        if (sidebar_hover != old_hover) {
            win->dirty = 1;
            return 1;
        }
    }

    return 0;
}

/* ═══ Custom draw: column headers ═════════════════════════════ */

static void fm_draw_col_hdr(ui_window_t *win, int widget_idx,
                              uint32_t *canvas, int cw, int ch) {
    ui_widget_t *wg = ui_get_widget(win, widget_idx);
    if (!wg) return;
    int x0 = wg->x, y0 = wg->y, w = wg->w, h = wg->h;

    gfx_buf_fill_rect(canvas, cw, ch, x0, y0, w, h, ui_theme.surface);

    const char *cols[] = { "Name", "Size", "Type" };
    int col_x[] = { x0 + FM_COL_NAME, x0 + w - FM_COL_SIZE_OFF, x0 + w - FM_COL_TYPE_OFF };

    for (int i = 0; i < 3; i++) {
        uint32_t fg = ui_theme.text_secondary;
        gfx_buf_draw_string(canvas, cw, ch, col_x[i], y0 + 4, cols[i], fg, ui_theme.surface);

        /* Sort indicator */
        if (i == fm_sort_col) {
            int lw = (int)strlen(cols[i]) * FONT_W;
            gfx_buf_fill_rect(canvas, cw, ch, col_x[i], y0 + h - 2, lw, 2, ui_theme.accent);
            /* Arrow */
            const char *arrow = fm_sort_asc ? " ^" : " v";
            gfx_buf_draw_string(canvas, cw, ch, col_x[i] + lw, y0 + 4,
                                 arrow, ui_theme.accent, ui_theme.surface);
        }
    }

    /* Bottom border */
    gfx_buf_fill_rect(canvas, cw, ch, x0, y0 + h - 1, w, 1, ui_theme.border);
}

static int fm_col_hdr_event(ui_window_t *win, int widget_idx, ui_event_t *ev) {
    if (ev->type != UI_EVENT_MOUSE_DOWN) return 0;
    ui_widget_t *wg = ui_get_widget(win, widget_idx);
    if (!wg) return 0;

    int wx = ev->mouse.wx - wg->x;
    int w = wg->w;

    int new_col;
    if (wx < w - FM_COL_SIZE_OFF - 20) new_col = 0;       /* Name */
    else if (wx < w - FM_COL_TYPE_OFF - 10) new_col = 1;   /* Size */
    else new_col = 2;                                        /* Type */

    if (new_col == fm_sort_col) {
        fm_sort_asc = !fm_sort_asc;
    } else {
        fm_sort_col = new_col;
        fm_sort_asc = 1;
    }

    fm_sort();
    fm_selected = 0;
    fm_scroll = 0;
    win->dirty = 1;
    return 1;
}

/* ═══ Custom draw: file list ══════════════════════════════════ */

static void fm_draw_filelist(ui_window_t *win, int widget_idx,
                               uint32_t *canvas, int cw, int ch) {
    ui_widget_t *wg = ui_get_widget(win, widget_idx);
    if (!wg) return;
    int x0 = wg->x, y0 = wg->y, w = wg->w, h = wg->h;

    /* Clear */
    gfx_buf_fill_rect(canvas, cw, ch, x0, y0, w, h, ui_theme.win_bg);

    int visible_rows = h / FM_ROW_H;
    if (visible_rows < 1) visible_rows = 1;

    /* Ensure scroll keeps selected visible */
    if (fm_selected < fm_scroll) fm_scroll = fm_selected;
    if (fm_selected >= fm_scroll + visible_rows) fm_scroll = fm_selected - visible_rows + 1;
    if (fm_scroll < 0) fm_scroll = 0;

    for (int vi = 0; vi < visible_rows; vi++) {
        int i = fm_scroll + vi;
        if (i >= fm_count) break;

        fs_dir_entry_info_t *e = &fm_entries[i];
        int ry = y0 + vi * FM_ROW_H;
        int selected = (i == fm_selected);

        /* Row background: selected, or alternating */
        uint32_t row_bg = selected ? ui_theme.list_sel_bg :
                          ((vi % 2) ? GFX_RGB(18, 18, 30) : ui_theme.win_bg);
        gfx_buf_fill_rect(canvas, cw, ch, x0, ry, w, FM_ROW_H, row_bg);

        /* Selection accent bar */
        if (selected)
            gfx_buf_fill_rect(canvas, cw, ch, x0 + 2, ry + 3, 3, FM_ROW_H - 6,
                               ui_theme.accent);

        /* Icon/type glyph */
        uint32_t icon_color;
        char icon_ch;
        switch (e->type) {
        case INODE_DIR:
            icon_ch = 'D';
            icon_color = GFX_RGB(200, 160, 80); /* gold folder */
            break;
        case INODE_SYMLINK:
            icon_ch = 'L';
            icon_color = GFX_RGB(80, 200, 200); /* cyan link */
            break;
        default:
            icon_ch = 'F';
            icon_color = GFX_RGB(160, 160, 180); /* gray file */
            break;
        }
        gfx_buf_draw_char(canvas, cw, ch, x0 + FM_COL_ICON,
                           ry + (FM_ROW_H - FONT_H) / 2,
                           icon_ch, icon_color, row_bg);

        /* Name (with trailing / for dirs) */
        char namebuf[40];
        if (e->type == INODE_DIR)
            snprintf(namebuf, sizeof(namebuf), "%s/", e->name);
        else
            snprintf(namebuf, sizeof(namebuf), "%s", e->name);

        /* Truncate name to fit */
        int name_max = (w - FM_COL_NAME - FM_COL_SIZE_OFF - 8) / FONT_W;
        if (name_max > 0 && (int)strlen(namebuf) > name_max) {
            namebuf[name_max - 1] = '.';
            namebuf[name_max] = '\0';
        }
        gfx_buf_draw_string(canvas, cw, ch, x0 + FM_COL_NAME,
                             ry + (FM_ROW_H - FONT_H) / 2,
                             namebuf, ui_theme.text_primary, row_bg);

        /* Size */
        char sizebuf[16];
        if (e->type == INODE_DIR) {
            strcpy(sizebuf, "--");
        } else if (e->size >= 1024) {
            snprintf(sizebuf, sizeof(sizebuf), "%dKB", (int)(e->size / 1024));
        } else {
            snprintf(sizebuf, sizeof(sizebuf), "%dB", (int)e->size);
        }
        gfx_buf_draw_string(canvas, cw, ch, x0 + w - FM_COL_SIZE_OFF,
                             ry + (FM_ROW_H - FONT_H) / 2,
                             sizebuf, ui_theme.text_sub, row_bg);

        /* Type */
        const char *type_str;
        switch (e->type) {
        case INODE_DIR:     type_str = "DIR"; break;
        case INODE_SYMLINK: type_str = "LNK"; break;
        default:            type_str = "FILE"; break;
        }
        gfx_buf_draw_string(canvas, cw, ch, x0 + w - FM_COL_TYPE_OFF,
                             ry + (FM_ROW_H - FONT_H) / 2,
                             type_str, ui_theme.text_dim, row_bg);
    }

    /* Scrollbar if needed */
    if (fm_count > visible_rows && visible_rows > 0) {
        int sb_x = x0 + w - 6;
        int sb_h = h;
        int thumb_h = (visible_rows * sb_h) / fm_count;
        if (thumb_h < 16) thumb_h = 16;
        int thumb_y = y0 + (fm_scroll * (sb_h - thumb_h)) /
                      (fm_count - visible_rows > 0 ? fm_count - visible_rows : 1);
        gfx_buf_fill_rect(canvas, cw, ch, sb_x, y0, 4, sb_h, GFX_RGB(30, 30, 45));
        gfx_buf_fill_rect(canvas, cw, ch, sb_x, thumb_y, 4, thumb_h, ui_theme.text_dim);
    }
}

static int fm_filelist_event(ui_window_t *win, int widget_idx, ui_event_t *ev) {
    ui_widget_t *wg = ui_get_widget(win, widget_idx);
    if (!wg) return 0;

    if (ev->type == UI_EVENT_MOUSE_DOWN) {
        int wy = ev->mouse.wy - wg->y;
        if (wy >= 0) {
            int clicked = fm_scroll + wy / FM_ROW_H;
            if (clicked >= 0 && clicked < fm_count) {
                if (clicked == fm_selected) {
                    /* Double-click: open */
                    fm_open_selected(win);
                    return 1;
                }
                fm_selected = clicked;
                win->dirty = 1;
                return 1;
            }
        }
    }

    return 0;
}

/* ═══ Custom draw: status bar ═════════════════════════════════ */

static void fm_draw_status(ui_window_t *win, int widget_idx,
                             uint32_t *canvas, int cw, int ch) {
    ui_widget_t *wg = ui_get_widget(win, widget_idx);
    if (!wg) return;
    int x0 = wg->x, y0 = wg->y, w = wg->w, h = wg->h;

    gfx_buf_fill_rect(canvas, cw, ch, x0, y0, w, h, ui_theme.surface);
    gfx_buf_fill_rect(canvas, cw, ch, x0, y0, w, 1, ui_theme.border);

    gfx_buf_draw_string(canvas, cw, ch, x0 + 8, y0 + 4,
                         fm_status_str, ui_theme.text_sub, ui_theme.surface);
}

/* ═══ Event handler ═══════════════════════════════════════════ */

void app_filemgr_on_event(ui_window_t *win, ui_event_t *ev) {
    if (ev->type == UI_EVENT_KEY_PRESS) {
        char key = ev->key.key;

        /* Backspace = go up */
        if (key == '\b') {
            fm_navigate_up(win);
            return;
        }

        /* Enter = open selected */
        if (key == '\n' || key == '\r') {
            fm_open_selected(win);
            return;
        }

        /* Delete = delete selected */
        if (key == 0x7F) {
            fm_delete_selected(win);
            return;
        }

        /* Navigate up/down */
        if (key == KEY_UP && fm_selected > 0) {
            fm_selected--;
            win->dirty = 1;
            return;
        }
        if (key == KEY_DOWN && fm_selected < fm_count - 1) {
            fm_selected++;
            win->dirty = 1;
            return;
        }

        /* Home / End */
        if (key == KEY_HOME) {
            fm_selected = 0;
            fm_scroll = 0;
            win->dirty = 1;
            return;
        }
        if (key == KEY_END) {
            fm_selected = fm_count > 0 ? fm_count - 1 : 0;
            win->dirty = 1;
            return;
        }
    }
}

void app_filemgr_on_close(ui_window_t *win) {
    (void)win;
    fs_change_directory(fm_orig_cwd);
}

/* ═══ Create ══════════════════════════════════════════════════ */

ui_window_t *app_filemgr_create(void) {
    int fb_w = (int)gfx_width(), fb_h = (int)gfx_height();
    int win_w = fb_w - 200;
    int win_h = fb_h - TASKBAR_H - 60;

    /* Save original directory */
    strncpy(fm_orig_cwd, fs_get_cwd(), sizeof(fm_orig_cwd) - 1);
    fm_orig_cwd[sizeof(fm_orig_cwd) - 1] = '\0';

    ui_window_t *win = ui_window_create(100, 20, win_w, win_h, "Files");
    if (!win) return 0;

    int cw, ch;
    wm_get_canvas(win->wm_id, &cw, &ch);

    /* Init state */
    fm_sort_col = 0;
    fm_sort_asc = 1;
    fm_selected = 0;
    fm_scroll = 0;
    fm_hist_pos = 0;
    fm_hist_len = 0;
    sidebar_hover = -1;

    /* Push initial location to history */
    fm_history_push();

    /* Navigation bar (full width, top) */
    w_nav_bar = ui_add_custom(win, 0, 0, cw, FM_NAV_H,
                               fm_draw_nav, fm_nav_event, NULL);

    /* Sidebar (left side, below nav) */
    int body_y = FM_NAV_H;
    int body_h = ch - FM_NAV_H - FM_STATUS_H;
    w_sidebar = ui_add_custom(win, 0, body_y, FM_SIDEBAR_W, body_h,
                               fm_draw_sidebar, fm_sidebar_event, NULL);

    /* Column headers (right of sidebar, below nav) */
    int list_x = FM_SIDEBAR_W;
    int list_w = cw - FM_SIDEBAR_W;
    w_col_hdr = ui_add_custom(win, list_x, body_y, list_w, FM_COL_HDR_H,
                               fm_draw_col_hdr, fm_col_hdr_event, NULL);

    /* File list (right of sidebar, below headers) */
    int fl_y = body_y + FM_COL_HDR_H;
    int fl_h = body_h - FM_COL_HDR_H;
    w_filelist = ui_add_custom(win, list_x, fl_y, list_w, fl_h,
                                fm_draw_filelist, fm_filelist_event, NULL);

    /* Status bar (full width, bottom) */
    w_status = ui_add_custom(win, 0, ch - FM_STATUS_H, cw, FM_STATUS_H,
                              fm_draw_status, NULL, NULL);

    /* Load current directory */
    fm_load_dir(win);

    /* Focus file list */
    if (win->focused_widget < 0)
        ui_focus_next(win);

    return win;
}

/* ═══ Standalone entry point ══════════════════════════════════ */

void app_filemgr(void) {
    ui_window_t *win = app_filemgr_create();
    if (!win) return;
    ui_app_run(win, app_filemgr_on_event);
    app_filemgr_on_close(win);
    ui_window_destroy(win);
}
