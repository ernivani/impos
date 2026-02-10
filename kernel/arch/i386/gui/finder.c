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

/* Built-in app entries */
#define FINDER_APP_COUNT 6

static const char *app_names[FINDER_APP_COUNT] = {
    "Files", "Terminal", "Activity Monitor", "Editor", "Settings", "System Monitor"
};
static const int app_actions[FINDER_APP_COUNT] = {
    DESKTOP_ACTION_FILES, DESKTOP_ACTION_TERMINAL, DESKTOP_ACTION_BROWSER,
    DESKTOP_ACTION_EDITOR, DESKTOP_ACTION_SETTINGS, DESKTOP_ACTION_MONITOR
};

/* Search state */
#define FINDER_MAX_RESULTS 10
#define FINDER_MAX_FILE_RESULTS 10
#define FINDER_QUERY_MAX 64

static char query[FINDER_QUERY_MAX];
static int query_len;

/* App results */
static const char *results[FINDER_MAX_RESULTS];
static int result_actions[FINDER_MAX_RESULTS];
static int result_scores[FINDER_MAX_RESULTS];
static int result_count;

/* File results */
static char file_results[FINDER_MAX_FILE_RESULTS][MAX_NAME_LEN + 64];
static int file_result_count;

/* Combined selection */
static int result_sel;
static int total_results;

/* Finder layout */
#define FINDER_W      500
#define FINDER_BAR_H   36
#define FINDER_ROW_H   28
#define FINDER_R       14
#define FINDER_CAT_H   22

static int finder_x, finder_y;

/* ═══ Fuzzy matching ═════════════════════════════════════════ */

static int fuzzy_score(const char *haystack, const char *needle) {
    if (!needle[0]) return 100;
    int score = 0;
    int ni = 0;
    int nlen = (int)strlen(needle);
    int hlen = (int)strlen(haystack);
    int consecutive = 0;

    for (int hi = 0; hi < hlen && ni < nlen; hi++) {
        char hc = haystack[hi];
        char nc = needle[ni];
        if (hc >= 'A' && hc <= 'Z') hc += 32;
        if (nc >= 'A' && nc <= 'Z') nc += 32;

        if (hc == nc) {
            score += 10;
            if (consecutive > 0) score += 5;
            if (hi == 0) score += 3;
            consecutive++;
            ni++;
        } else {
            consecutive = 0;
        }
    }

    /* Must match all characters in needle */
    if (ni < nlen) return 0;
    return score;
}

/* ═══ File search ═════════════════════════════════════════════ */

static void finder_search_files_in(const char *path, int depth) {
    if (depth > 3 || file_result_count >= FINDER_MAX_FILE_RESULTS) return;

    uint32_t saved = fs_get_cwd_inode();
    if (fs_change_directory(path) != 0) {
        fs_change_directory_by_inode(saved);
        return;
    }

    fs_dir_entry_info_t entries[32];
    int count = fs_enumerate_directory(entries, 32, 0);

    for (int i = 0; i < count && file_result_count < FINDER_MAX_FILE_RESULTS; i++) {
        if (entries[i].name[0] == '.') continue;

        /* Check fuzzy match */
        int score = fuzzy_score(entries[i].name, query);
        if (score > 0) {
            snprintf(file_results[file_result_count], sizeof(file_results[0]),
                     "%s/%s", path, entries[i].name);
            file_result_count++;
        }

        /* Recurse into directories */
        if (entries[i].type == INODE_DIR && depth < 3) {
            char subpath[128];
            snprintf(subpath, sizeof(subpath), "%s/%s", path, entries[i].name);
            uint32_t cur = fs_get_cwd_inode();
            finder_search_files_in(subpath, depth + 1);
            fs_change_directory_by_inode(cur);
        }
    }

    fs_change_directory_by_inode(saved);
}

/* ═══ Search ══════════════════════════════════════════════════ */

static void finder_search(void) {
    result_count = 0;
    file_result_count = 0;
    result_sel = 0;

    /* Match built-in apps with fuzzy scoring */
    for (int i = 0; i < FINDER_APP_COUNT; i++) {
        int score = query_len > 0 ? fuzzy_score(app_names[i], query) : 100;
        if (score > 0 && result_count < FINDER_MAX_RESULTS) {
            results[result_count] = app_names[i];
            result_actions[result_count] = app_actions[i];
            result_scores[result_count] = score;
            result_count++;
        }
    }

    /* Sort app results by score descending */
    for (int i = 0; i < result_count - 1; i++) {
        for (int j = i + 1; j < result_count; j++) {
            if (result_scores[j] > result_scores[i]) {
                const char *tn = results[i];
                results[i] = results[j];
                results[j] = tn;
                int ta = result_actions[i];
                result_actions[i] = result_actions[j];
                result_actions[j] = ta;
                int ts = result_scores[i];
                result_scores[i] = result_scores[j];
                result_scores[j] = ts;
            }
        }
    }

    /* Search files if query is non-empty */
    if (query_len > 0) {
        const char *user = user_get_current();
        if (user) {
            char home[128];
            snprintf(home, sizeof(home), "/home/%s", user);
            finder_search_files_in(home, 0);
        }
    }

    total_results = result_count + file_result_count;
}

/* ═══ Drawing ═════════════════════════════════════════════════ */

static void finder_draw(void) {
    int fb_w = (int)gfx_width(), fb_h = (int)gfx_height();
    finder_x = fb_w / 2 - FINDER_W / 2;
    finder_y = fb_h / 3 - FINDER_BAR_H / 2;

    /* Darken entire screen with overlay */
    uint32_t *bb = gfx_backbuffer();
    uint32_t pitch4 = gfx_pitch() / 4;
    for (int y = 0; y < fb_h; y++) {
        for (int x = 0; x < fb_w; x++) {
            uint32_t px = bb[y * pitch4 + x];
            uint32_t r = ((px >> 16) & 0xFF) * 120 / 255;
            uint32_t g = ((px >> 8) & 0xFF) * 120 / 255;
            uint32_t b = (px & 0xFF) * 120 / 255;
            bb[y * pitch4 + x] = (r << 16) | (g << 8) | b;
        }
    }

    /* Calculate total height */
    int total_h = FINDER_BAR_H;
    int has_apps = result_count > 0;
    int has_files = file_result_count > 0;
    if (has_apps || has_files)
        total_h += 2;
    if (has_apps)
        total_h += FINDER_CAT_H + result_count * FINDER_ROW_H;
    if (has_files)
        total_h += FINDER_CAT_H + file_result_count * FINDER_ROW_H;
    if (has_apps || has_files)
        total_h += 4;

    /* Search bar pill */
    gfx_rounded_rect_alpha(finder_x, finder_y, FINDER_W, total_h,
                           FINDER_R, GFX_RGB(38, 36, 44), 230);
    gfx_rounded_rect_outline(finder_x, finder_y, FINDER_W, total_h,
                             FINDER_R, GFX_RGB(85, 82, 94));

    /* Search icon (magnifying glass) */
    int icon_x = finder_x + 16;
    int icon_cy = finder_y + FINDER_BAR_H / 2;
    gfx_circle_ring(icon_x, icon_cy - 1, 5, 1, GFX_RGB(150, 148, 160));
    gfx_draw_line(icon_x + 4, icon_cy + 3, icon_x + 7, icon_cy + 6,
                  GFX_RGB(150, 148, 160));

    /* Query text */
    int tx = finder_x + 32;
    int ty = finder_y + (FINDER_BAR_H - FONT_H) / 2;
    if (query_len == 0) {
        gfx_draw_string_nobg(tx, ty, "Search apps & files...", GFX_RGB(100, 98, 110));
    } else {
        for (int i = 0; i < query_len; i++)
            gfx_draw_char_nobg(tx + i * FONT_W, ty, query[i],
                                GFX_RGB(220, 218, 230));
    }

    /* Cursor */
    int cx = tx + query_len * FONT_W;
    gfx_fill_rect(cx, ty, 1, FONT_H, GFX_RGB(200, 198, 210));

    /* Results */
    if (has_apps || has_files) {
        int ry = finder_y + FINDER_BAR_H + 2;
        int sel_idx = 0;

        /* Separator */
        gfx_fill_rect(finder_x + 12, ry - 1, FINDER_W - 24, 1,
                       GFX_RGB(60, 58, 68));

        /* Apps section */
        if (has_apps) {
            gfx_draw_string_nobg(finder_x + 12, ry + (FINDER_CAT_H - FONT_H) / 2,
                                  "Apps", GFX_RGB(120, 118, 140));
            ry += FINDER_CAT_H;

            for (int i = 0; i < result_count; i++) {
                int row_y = ry + i * FINDER_ROW_H;
                int is_selected = (sel_idx == result_sel);

                if (is_selected) {
                    gfx_rounded_rect_alpha(finder_x + 6, row_y + 2,
                                           FINDER_W - 12, FINDER_ROW_H - 4, 6,
                                           GFX_RGB(80, 120, 200), 140);
                }

                uint32_t tc = is_selected ? GFX_RGB(255, 255, 255)
                                           : GFX_RGB(190, 188, 200);
                gfx_draw_string_nobg(finder_x + 16,
                                     row_y + (FINDER_ROW_H - FONT_H) / 2,
                                     results[i], tc);
                sel_idx++;
            }
            ry += result_count * FINDER_ROW_H;
        }

        /* Files section */
        if (has_files) {
            gfx_draw_string_nobg(finder_x + 12, ry + (FINDER_CAT_H - FONT_H) / 2,
                                  "Files", GFX_RGB(120, 118, 140));
            ry += FINDER_CAT_H;

            for (int i = 0; i < file_result_count; i++) {
                int row_y = ry + i * FINDER_ROW_H;
                int is_selected = (sel_idx == result_sel);

                if (is_selected) {
                    gfx_rounded_rect_alpha(finder_x + 6, row_y + 2,
                                           FINDER_W - 12, FINDER_ROW_H - 4, 6,
                                           GFX_RGB(80, 120, 200), 140);
                }

                uint32_t tc = is_selected ? GFX_RGB(255, 255, 255)
                                           : GFX_RGB(190, 188, 200);
                /* Show filename only (truncated path) */
                const char *path = file_results[i];
                const char *fname = path;
                for (const char *p = path; *p; p++)
                    if (*p == '/') fname = p + 1;

                char display[64];
                snprintf(display, sizeof(display), "%s", fname);
                gfx_draw_string_nobg(finder_x + 16,
                                     row_y + (FINDER_ROW_H - FONT_H) / 2,
                                     display, tc);

                /* Show path dimmed on the right */
                int dw = (int)strlen(display) * FONT_W;
                int path_x = finder_x + 16 + dw + 16;
                if (path_x < finder_x + FINDER_W - 100) {
                    gfx_draw_string_nobg(path_x,
                                         row_y + (FINDER_ROW_H - FONT_H) / 2,
                                         path, GFX_RGB(80, 78, 100));
                }

                sel_idx++;
            }
        }
    }

    gfx_flip();
    gfx_draw_mouse_cursor(mouse_get_x(), mouse_get_y());
}

static void finder_idle(void) {
    if (mouse_poll()) {
        gfx_draw_mouse_cursor(mouse_get_x(), mouse_get_y());
    }
}

int finder_show(void) {
    query_len = 0;
    query[0] = '\0';

    /* Initial search shows all apps */
    finder_search();

    /* Register Finder as a tracked process */
    int finder_tid = task_register("Finder", 1, -1);

    keyboard_set_idle_callback(finder_idle);

    while (1) {
        finder_draw();

        char c = getchar();

        /* Check double-ctrl to dismiss */
        if (keyboard_check_double_ctrl()) {
            if (finder_tid >= 0) task_unregister(finder_tid);
            keyboard_set_idle_callback(0);
            return 0;
        }

        /* Alt+Space also dismisses */
        if (c == KEY_FINDER) {
            if (finder_tid >= 0) task_unregister(finder_tid);
            keyboard_set_idle_callback(0);
            return 0;
        }

        if (c == KEY_ESCAPE) {
            if (finder_tid >= 0) task_unregister(finder_tid);
            keyboard_set_idle_callback(0);
            return 0;
        }

        if (c == '\n') {
            if (finder_tid >= 0) task_unregister(finder_tid);
            keyboard_set_idle_callback(0);
            if (total_results > 0 && result_sel < total_results) {
                if (result_sel < result_count) {
                    return result_actions[result_sel];
                } else {
                    /* File result — open Files app */
                    return DESKTOP_ACTION_FILES;
                }
            }
            return 0;
        }

        if (c == KEY_UP) {
            if (result_sel > 0) result_sel--;
            continue;
        }
        if (c == KEY_DOWN) {
            if (result_sel < total_results - 1) result_sel++;
            continue;
        }

        if (c == '\b') {
            if (query_len > 0) {
                query_len--;
                query[query_len] = '\0';
                finder_search();
            }
            continue;
        }

        /* Filter special keys */
        if ((unsigned char)c >= 0xB0 && (unsigned char)c <= 0xBC) continue;
        if (c < 32 || c >= 127) continue;

        /* Add character to query */
        if (query_len < FINDER_QUERY_MAX - 1) {
            query[query_len++] = c;
            query[query_len] = '\0';
            finder_search();
        }
    }
}
