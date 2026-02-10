#include <kernel/finder.h>
#include <kernel/gfx.h>
#include <kernel/desktop.h>
#include <kernel/wm.h>
#include <kernel/mouse.h>
#include <kernel/fs.h>
#include <kernel/idt.h>
#include <kernel/ui_theme.h>
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
#define FINDER_QUERY_MAX 64

static char query[FINDER_QUERY_MAX];
static int query_len;

static const char *results[FINDER_MAX_RESULTS];
static int result_actions[FINDER_MAX_RESULTS];
static int result_count;
static int result_sel;

/* Finder layout */
#define FINDER_W      500
#define FINDER_BAR_H   36
#define FINDER_ROW_H   28
#define FINDER_R       14

static int finder_x, finder_y;

static int str_contains_ci(const char *haystack, const char *needle) {
    if (!needle[0]) return 1;
    int nlen = (int)strlen(needle);
    int hlen = (int)strlen(haystack);
    for (int i = 0; i <= hlen - nlen; i++) {
        int match = 1;
        for (int j = 0; j < nlen; j++) {
            char hc = haystack[i + j];
            char nc = needle[j];
            if (hc >= 'A' && hc <= 'Z') hc += 32;
            if (nc >= 'A' && nc <= 'Z') nc += 32;
            if (hc != nc) { match = 0; break; }
        }
        if (match) return 1;
    }
    return 0;
}

static void finder_search(void) {
    result_count = 0;
    result_sel = 0;

    /* Match built-in apps */
    for (int i = 0; i < FINDER_APP_COUNT && result_count < FINDER_MAX_RESULTS; i++) {
        if (str_contains_ci(app_names[i], query)) {
            results[result_count] = app_names[i];
            result_actions[result_count] = app_actions[i];
            result_count++;
        }
    }
}

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

    int total_h = FINDER_BAR_H;
    if (result_count > 0)
        total_h += 2 + result_count * FINDER_ROW_H + 4;

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
        gfx_draw_string_nobg(tx, ty, "Search...", GFX_RGB(100, 98, 110));
    } else {
        for (int i = 0; i < query_len; i++)
            gfx_draw_char_nobg(tx + i * FONT_W, ty, query[i],
                                GFX_RGB(220, 218, 230));
    }

    /* Cursor */
    int cx = tx + query_len * FONT_W;
    gfx_fill_rect(cx, ty, 1, FONT_H, GFX_RGB(200, 198, 210));

    /* Results */
    if (result_count > 0) {
        int ry = finder_y + FINDER_BAR_H + 2;

        /* Separator */
        gfx_fill_rect(finder_x + 12, ry - 1, FINDER_W - 24, 1,
                       GFX_RGB(60, 58, 68));

        for (int i = 0; i < result_count; i++) {
            int row_y = ry + i * FINDER_ROW_H;

            /* Highlight selected */
            if (i == result_sel) {
                gfx_rounded_rect_alpha(finder_x + 6, row_y + 2,
                                       FINDER_W - 12, FINDER_ROW_H - 4, 6,
                                       GFX_RGB(80, 120, 200), 140);
            }

            uint32_t tc = (i == result_sel) ? GFX_RGB(255, 255, 255)
                                             : GFX_RGB(190, 188, 200);
            gfx_draw_string_nobg(finder_x + 16,
                                 row_y + (FINDER_ROW_H - FONT_H) / 2,
                                 results[i], tc);
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

    keyboard_set_idle_callback(finder_idle);

    while (1) {
        finder_draw();

        char c = getchar();

        /* Check double-ctrl to dismiss */
        if (keyboard_check_double_ctrl()) {
            keyboard_set_idle_callback(0);
            return 0;
        }

        if (c == KEY_ESCAPE) {
            keyboard_set_idle_callback(0);
            return 0;
        }

        if (c == '\n') {
            keyboard_set_idle_callback(0);
            if (result_count > 0 && result_sel < result_count)
                return result_actions[result_sel];
            return 0;
        }

        if (c == KEY_UP) {
            if (result_sel > 0) result_sel--;
            continue;
        }
        if (c == KEY_DOWN) {
            if (result_sel < result_count - 1) result_sel++;
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
