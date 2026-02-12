#include <kernel/finder.h>
#include <kernel/gfx.h>
#include <kernel/desktop.h>
#include <kernel/wm.h>
#include <kernel/mouse.h>
#include <kernel/fs.h>
#include <kernel/user.h>
#include <kernel/idt.h>
#include <kernel/ui_theme.h>
#include <kernel/task.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ═══ Searchable items ════════════════════════════════════════ */

/* Built-in apps */
#define APP_COUNT 8

static const char *app_names[APP_COUNT] = {
    "Files", "Terminal", "Activity Monitor", "Editor",
    "Settings", "System Monitor", "Power", "Trash"
};
static const char *app_descs[APP_COUNT] = {
    "Browse and manage files",
    "Command-line shell",
    "View running processes",
    "Text editor (vi)",
    "System preferences",
    "CPU and memory usage",
    "Shutdown or restart",
    "Empty trash"
};
static const int app_actions[APP_COUNT] = {
    DESKTOP_ACTION_FILES, DESKTOP_ACTION_TERMINAL, DESKTOP_ACTION_BROWSER,
    DESKTOP_ACTION_EDITOR, DESKTOP_ACTION_SETTINGS, DESKTOP_ACTION_MONITOR,
    DESKTOP_ACTION_POWER, DESKTOP_ACTION_TRASH
};

/* ═══ Search state ════════════════════════════════════════════ */

#define QUERY_MAX        64
#define MAX_APP_RESULTS  6
#define MAX_FILE_RESULTS 8

static char query[QUERY_MAX];
static int  query_len;

/* App results */
static int app_indices[MAX_APP_RESULTS];   /* index into app_names[] */
static int app_scores[MAX_APP_RESULTS];
static int app_result_count;

/* File results */
typedef struct {
    char path[128];     /* full path e.g. /home/user/readme.txt */
    char name[MAX_NAME_LEN];
    int  is_dir;
    int  score;
} file_result_t;

static file_result_t file_results[MAX_FILE_RESULTS];
static int file_result_count;

/* Selection */
static int result_sel;
static int total_results;

/* Mouse state */
static int  finder_click_action;
static uint8_t finder_prev_btns;

/* Layout */
#define FINDER_W       520
#define FINDER_BAR_H    40
#define FINDER_ROW_H    32
#define FINDER_R        12
#define FINDER_CAT_H    24
#define FINDER_PAD      8

static int finder_x, finder_y;

/* Saved backbuffer */
static uint32_t *finder_saved_bb = 0;

/* ═══ Fuzzy match ═════════════════════════════════════════════ */

static char to_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

static int fuzzy_score(const char *haystack, const char *needle) {
    if (!needle[0]) return 0;  /* empty query = no match */

    int score = 0, ni = 0;
    int nlen = (int)strlen(needle);
    int hlen = (int)strlen(haystack);
    int consecutive = 0;
    int first_match = -1;

    for (int hi = 0; hi < hlen && ni < nlen; hi++) {
        if (to_lower(haystack[hi]) == to_lower(needle[ni])) {
            score += 10;
            if (first_match < 0) first_match = hi;
            if (consecutive > 0) score += 5 * consecutive;
            if (hi == 0) score += 15;           /* start of string */
            if (hi > 0 && (haystack[hi-1] == ' ' || haystack[hi-1] == '/'
                        || haystack[hi-1] == '.'))
                score += 10;                     /* word boundary */
            consecutive++;
            ni++;
        } else {
            consecutive = 0;
        }
    }

    if (ni < nlen) return 0;  /* didn't match all chars */

    /* Prefer shorter strings (more relevant) */
    score += 50 / (hlen + 1);
    /* Prefer earlier first match */
    if (first_match >= 0) score += 10 / (first_match + 1);

    return score;
}

/* ═══ File indexing (recursive) ═══════════════════════════════ */

static void index_files_in(const char *path, int depth) {
    if (depth > 3 || file_result_count >= MAX_FILE_RESULTS) return;

    uint32_t saved = fs_get_cwd_inode();
    if (fs_change_directory(path) != 0) {
        fs_change_directory_by_inode(saved);
        return;
    }

    fs_dir_entry_info_t entries[32];
    int count = fs_enumerate_directory(entries, 32, 0);

    for (int i = 0; i < count && file_result_count < MAX_FILE_RESULTS; i++) {
        if (entries[i].name[0] == '.') continue;

        int score = fuzzy_score(entries[i].name, query);
        if (score > 0) {
            file_result_t *fr = &file_results[file_result_count];
            snprintf(fr->path, sizeof(fr->path), "%s/%s", path, entries[i].name);
            strncpy(fr->name, entries[i].name, MAX_NAME_LEN - 1);
            fr->name[MAX_NAME_LEN - 1] = '\0';
            fr->is_dir = (entries[i].type == INODE_DIR);
            fr->score = score;
            file_result_count++;
        }

        if (entries[i].type == INODE_DIR && depth < 3) {
            char sub[128];
            snprintf(sub, sizeof(sub), "%s/%s", path, entries[i].name);
            uint32_t cur = fs_get_cwd_inode();
            index_files_in(sub, depth + 1);
            fs_change_directory_by_inode(cur);
        }
    }

    fs_change_directory_by_inode(saved);
}

/* ═══ Search ══════════════════════════════════════════════════ */

static void finder_search(void) {
    app_result_count = 0;
    file_result_count = 0;
    result_sel = 0;

    /* Empty query → show nothing (Spotlight-style) */
    if (query_len == 0) {
        total_results = 0;
        return;
    }

    /* Score apps */
    for (int i = 0; i < APP_COUNT; i++) {
        int score = fuzzy_score(app_names[i], query);
        /* Also match against description */
        int dscore = fuzzy_score(app_descs[i], query);
        if (dscore > score) score = dscore;

        if (score > 0 && app_result_count < MAX_APP_RESULTS) {
            /* Insert sorted by score descending */
            int pos = app_result_count;
            while (pos > 0 && app_scores[pos - 1] < score) {
                if (pos < MAX_APP_RESULTS) {
                    app_indices[pos] = app_indices[pos - 1];
                    app_scores[pos] = app_scores[pos - 1];
                }
                pos--;
            }
            app_indices[pos] = i;
            app_scores[pos] = score;
            app_result_count++;
        }
    }

    /* Search files */
    const char *user = user_get_current();
    if (user) {
        char home[128];
        snprintf(home, sizeof(home), "/home/%s", user);
        index_files_in(home, 0);
    }
    /* Also search /apps */
    index_files_in("/apps", 0);
    /* Also search root level */
    index_files_in("/", 0);

    /* Sort file results by score descending */
    for (int i = 0; i < file_result_count - 1; i++) {
        for (int j = i + 1; j < file_result_count; j++) {
            if (file_results[j].score > file_results[i].score) {
                file_result_t tmp = file_results[i];
                file_results[i] = file_results[j];
                file_results[j] = tmp;
            }
        }
    }

    total_results = app_result_count + file_result_count;
}

/* ═══ Drawing ═════════════════════════════════════════════════ */

/* Category label colors */
#define COL_CAT_TEXT     GFX_RGB(130, 128, 150)
#define COL_BG           GFX_RGB(30, 28, 36)
#define COL_BORDER       GFX_RGB(70, 68, 80)
#define COL_SEARCH_TEXT  GFX_RGB(220, 218, 230)
#define COL_PLACEHOLDER  GFX_RGB(90, 88, 100)
#define COL_CURSOR       GFX_RGB(100, 160, 255)
#define COL_RESULT_TEXT  GFX_RGB(210, 208, 220)
#define COL_RESULT_DIM   GFX_RGB(110, 108, 130)
#define COL_SEL_BG       GFX_RGB(60, 100, 200)
#define COL_SEL_TEXT     GFX_RGB(255, 255, 255)
#define COL_SEPARATOR    GFX_RGB(55, 53, 65)

static int calc_total_height(void) {
    int h = FINDER_BAR_H;
    if (total_results == 0) return h;

    h += FINDER_PAD; /* gap after search bar */
    if (app_result_count > 0)
        h += FINDER_CAT_H + app_result_count * FINDER_ROW_H;
    if (app_result_count > 0 && file_result_count > 0)
        h += FINDER_PAD; /* gap between sections */
    if (file_result_count > 0)
        h += FINDER_CAT_H + file_result_count * FINDER_ROW_H;
    h += FINDER_PAD; /* bottom padding */
    return h;
}

static void finder_draw(void) {
    int fb_w = (int)gfx_width(), fb_h = (int)gfx_height();
    finder_x = fb_w / 2 - FINDER_W / 2;
    finder_y = fb_h / 4;

    /* Restore saved backbuffer to prevent compounding darkening */
    uint32_t *bb = gfx_backbuffer();
    uint32_t pitch4 = gfx_pitch() / 4;
    if (finder_saved_bb)
        memcpy(bb, finder_saved_bb, (size_t)fb_h * gfx_pitch());

    /* Dim the entire screen */
    for (int y = 0; y < fb_h; y++) {
        for (int x = 0; x < fb_w; x++) {
            uint32_t px = bb[y * pitch4 + x];
            uint32_t r = ((px >> 16) & 0xFF) * 100 / 255;
            uint32_t g = ((px >> 8)  & 0xFF) * 100 / 255;
            uint32_t b = (px         & 0xFF) * 100 / 255;
            bb[y * pitch4 + x] = (r << 16) | (g << 8) | b;
        }
    }

    int total_h = calc_total_height();

    /* Main container */
    gfx_rounded_rect_alpha(finder_x, finder_y, FINDER_W, total_h,
                           FINDER_R, COL_BG, 240);
    gfx_rounded_rect_outline(finder_x, finder_y, FINDER_W, total_h,
                             FINDER_R, COL_BORDER);

    /* ── Search bar ────────────────────────────────────────── */

    /* Magnifying glass icon */
    int icon_x = finder_x + 18;
    int icon_cy = finder_y + FINDER_BAR_H / 2;
    gfx_circle_ring(icon_x, icon_cy - 1, 6, 1, COL_PLACEHOLDER);
    gfx_draw_line(icon_x + 4, icon_cy + 4, icon_x + 8, icon_cy + 8, COL_PLACEHOLDER);

    int tx = finder_x + 38;
    int ty = finder_y + (FINDER_BAR_H - FONT_H) / 2;

    if (query_len == 0) {
        gfx_draw_string_nobg(tx, ty, "Search...", COL_PLACEHOLDER);
    } else {
        for (int i = 0; i < query_len; i++)
            gfx_draw_char_nobg(tx + i * FONT_W, ty, query[i], COL_SEARCH_TEXT);
    }

    /* Blinking cursor */
    int cx = tx + query_len * FONT_W;
    gfx_fill_rect(cx, ty + 1, 2, FONT_H - 2, COL_CURSOR);

    /* ── Results ───────────────────────────────────────────── */

    if (total_results == 0) goto done;

    /* Separator below search bar */
    int ry = finder_y + FINDER_BAR_H + FINDER_PAD / 2;
    gfx_fill_rect(finder_x + 14, ry - 2, FINDER_W - 28, 1, COL_SEPARATOR);

    ry = finder_y + FINDER_BAR_H + FINDER_PAD;
    int sel_idx = 0;

    /* Apps section */
    if (app_result_count > 0) {
        gfx_draw_string_nobg(finder_x + 14, ry + (FINDER_CAT_H - FONT_H) / 2,
                             "Applications", COL_CAT_TEXT);
        ry += FINDER_CAT_H;

        for (int i = 0; i < app_result_count; i++) {
            int row_y = ry + i * FINDER_ROW_H;
            int selected = (sel_idx == result_sel);
            int ai = app_indices[i];

            if (selected) {
                gfx_rounded_rect_alpha(finder_x + 6, row_y + 1,
                                       FINDER_W - 12, FINDER_ROW_H - 2, 6,
                                       COL_SEL_BG, 180);
            }

            uint32_t name_col = selected ? COL_SEL_TEXT : COL_RESULT_TEXT;
            uint32_t desc_col = selected ? GFX_RGB(200, 210, 255) : COL_RESULT_DIM;

            gfx_draw_string_nobg(finder_x + 18,
                                 row_y + (FINDER_ROW_H - FONT_H) / 2,
                                 app_names[ai], name_col);

            /* Description on the right */
            int name_w = (int)strlen(app_names[ai]) * FONT_W;
            int desc_x = finder_x + 18 + name_w + 20;
            if (desc_x < finder_x + FINDER_W - 40)
                gfx_draw_string_nobg(desc_x,
                                     row_y + (FINDER_ROW_H - FONT_H) / 2,
                                     app_descs[ai], desc_col);

            sel_idx++;
        }
        ry += app_result_count * FINDER_ROW_H;
    }

    /* Gap + separator between sections */
    if (app_result_count > 0 && file_result_count > 0) {
        gfx_fill_rect(finder_x + 14, ry + FINDER_PAD / 2 - 1,
                       FINDER_W - 28, 1, COL_SEPARATOR);
        ry += FINDER_PAD;
    }

    /* Files section */
    if (file_result_count > 0) {
        gfx_draw_string_nobg(finder_x + 14, ry + (FINDER_CAT_H - FONT_H) / 2,
                             "Files & Folders", COL_CAT_TEXT);
        ry += FINDER_CAT_H;

        for (int i = 0; i < file_result_count; i++) {
            int row_y = ry + i * FINDER_ROW_H;
            int selected = (sel_idx == result_sel);
            file_result_t *fr = &file_results[i];

            if (selected) {
                gfx_rounded_rect_alpha(finder_x + 6, row_y + 1,
                                       FINDER_W - 12, FINDER_ROW_H - 2, 6,
                                       COL_SEL_BG, 180);
            }

            uint32_t name_col = selected ? COL_SEL_TEXT : COL_RESULT_TEXT;
            uint32_t path_col = selected ? GFX_RGB(200, 210, 255) : COL_RESULT_DIM;

            /* File/dir icon hint */
            const char *icon = fr->is_dir ? "/" : "";
            char display[80];
            snprintf(display, sizeof(display), "%s%s", fr->name, icon);

            gfx_draw_string_nobg(finder_x + 18,
                                 row_y + (FINDER_ROW_H - FONT_H) / 2,
                                 display, name_col);

            /* Path on the right */
            int name_w = (int)strlen(display) * FONT_W;
            int path_x = finder_x + 18 + name_w + 16;
            if (path_x < finder_x + FINDER_W - 20)
                gfx_draw_string_nobg(path_x,
                                     row_y + (FINDER_ROW_H - FONT_H) / 2,
                                     fr->path, path_col);

            sel_idx++;
        }
    }

done:
    gfx_flip();
    gfx_draw_mouse_cursor(mouse_get_x(), mouse_get_y());
}

/* ═══ Idle callback (mouse handling) ══════════════════════════ */

static void finder_idle(void) {
    if (!mouse_poll()) return;

    int mx = mouse_get_x(), my = mouse_get_y();
    gfx_draw_mouse_cursor(mx, my);

    uint8_t btns = mouse_get_buttons();
    int left_click = (btns & MOUSE_BTN_LEFT) && !(finder_prev_btns & MOUSE_BTN_LEFT);
    finder_prev_btns = btns;

    /* Hover tracking */
    if (total_results > 0) {
        int ry = finder_y + FINDER_BAR_H + FINDER_PAD;
        int sel_idx = 0, hover = -1;

        if (app_result_count > 0) {
            ry += FINDER_CAT_H;
            for (int i = 0; i < app_result_count; i++) {
                int row_y = ry + i * FINDER_ROW_H;
                if (my >= row_y && my < row_y + FINDER_ROW_H &&
                    mx >= finder_x && mx < finder_x + FINDER_W)
                    hover = sel_idx;
                sel_idx++;
            }
            ry += app_result_count * FINDER_ROW_H;
        }
        if (app_result_count > 0 && file_result_count > 0)
            ry += FINDER_PAD;
        if (file_result_count > 0) {
            ry += FINDER_CAT_H;
            for (int i = 0; i < file_result_count; i++) {
                int row_y = ry + i * FINDER_ROW_H;
                if (my >= row_y && my < row_y + FINDER_ROW_H &&
                    mx >= finder_x && mx < finder_x + FINDER_W)
                    hover = sel_idx;
                sel_idx++;
            }
        }
        if (hover >= 0) result_sel = hover;
    }

    if (!left_click) return;

    /* Click outside → dismiss */
    int total_h = calc_total_height();
    if (mx < finder_x || mx >= finder_x + FINDER_W ||
        my < finder_y || my >= finder_y + total_h) {
        finder_click_action = -1;
        keyboard_request_force_exit();
        return;
    }

    /* Click on a result row */
    if (total_results > 0) {
        int ry = finder_y + FINDER_BAR_H + FINDER_PAD;
        int sel_idx = 0;

        if (app_result_count > 0) {
            ry += FINDER_CAT_H;
            for (int i = 0; i < app_result_count; i++) {
                int row_y = ry + i * FINDER_ROW_H;
                if (my >= row_y && my < row_y + FINDER_ROW_H &&
                    mx >= finder_x + 6 && mx < finder_x + FINDER_W - 6) {
                    finder_click_action = app_actions[app_indices[i]];
                    keyboard_request_force_exit();
                    return;
                }
                sel_idx++;
            }
            ry += app_result_count * FINDER_ROW_H;
        }
        if (app_result_count > 0 && file_result_count > 0)
            ry += FINDER_PAD;
        if (file_result_count > 0) {
            ry += FINDER_CAT_H;
            for (int i = 0; i < file_result_count; i++) {
                int row_y = ry + i * FINDER_ROW_H;
                if (my >= row_y && my < row_y + FINDER_ROW_H &&
                    mx >= finder_x + 6 && mx < finder_x + FINDER_W - 6) {
                    finder_click_action = DESKTOP_ACTION_FILES;
                    keyboard_request_force_exit();
                    return;
                }
                sel_idx++;
            }
        }
    }
}

/* ═══ Cleanup helper ══════════════════════════════════════════ */

static int finder_cleanup(int finder_tid, int action) {
    if (finder_tid >= 0) task_unregister(finder_tid);
    keyboard_set_idle_callback(0);
    if (finder_saved_bb) { free(finder_saved_bb); finder_saved_bb = 0; }
    return action;
}

/* ═══ Main entry point ════════════════════════════════════════ */

int finder_show(void) {
    query_len = 0;
    query[0] = '\0';
    app_result_count = 0;
    file_result_count = 0;
    total_results = 0;
    result_sel = 0;

    /* Save backbuffer */
    {
        int fb_h = (int)gfx_height();
        size_t bb_size = (size_t)fb_h * gfx_pitch();
        finder_saved_bb = malloc(bb_size);
        if (finder_saved_bb)
            memcpy(finder_saved_bb, gfx_backbuffer(), bb_size);
    }

    int finder_tid = task_register("Finder", 1, -1);

    finder_click_action = 0;
    finder_prev_btns = mouse_get_buttons();
    keyboard_set_idle_callback(finder_idle);

    while (1) {
        finder_draw();

        char c = getchar();

        /* Mouse click action from idle callback */
        if (finder_click_action != 0) {
            int action = finder_click_action;
            finder_click_action = 0;
            return finder_cleanup(finder_tid, action > 0 ? action : 0);
        }

        /* Dismiss shortcuts */
        if (keyboard_check_double_ctrl() || c == KEY_FINDER || c == KEY_ESCAPE)
            return finder_cleanup(finder_tid, 0);

        /* Enter → activate selected result */
        if (c == '\n') {
            if (total_results > 0 && result_sel < total_results) {
                if (result_sel < app_result_count)
                    return finder_cleanup(finder_tid,
                                          app_actions[app_indices[result_sel]]);
                else
                    return finder_cleanup(finder_tid, DESKTOP_ACTION_FILES);
            }
            return finder_cleanup(finder_tid, 0);
        }

        /* Arrow keys */
        if (c == KEY_UP)   { if (result_sel > 0) result_sel--; continue; }
        if (c == KEY_DOWN) { if (result_sel < total_results - 1) result_sel++; continue; }

        /* Backspace */
        if (c == '\b') {
            if (query_len > 0) {
                query[--query_len] = '\0';
                finder_search();
            }
            continue;
        }

        /* Filter non-printable / special keys */
        if ((unsigned char)c >= 0xB0 && (unsigned char)c <= 0xBC) continue;
        if (c < 32 || c >= 127) continue;

        /* Type character → search */
        if (query_len < QUERY_MAX - 1) {
            query[query_len++] = c;
            query[query_len] = '\0';
            finder_search();
        }
    }
}
