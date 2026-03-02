/* minesweeper.c — Minesweeper game using widget toolkit (Phase 7.5)
 *
 * 9x9 grid, 10 mines (beginner difficulty).
 * Uses UI_CUSTOM widget for the board + buttons for controls.
 * Left-click reveals, right-click flags (via secondary click cb).
 */
#include <kernel/ui_widget.h>
#include <kernel/ui_window.h>
#include <kernel/ui_theme.h>
#include <kernel/ui_event.h>
#include <kernel/gfx.h>
#include <kernel/idt.h>
#include <string.h>
#include <stdio.h>

/* ── Constants ─────────────────────────────────────────────────── */

#define GRID_W    9
#define GRID_H    9
#define NUM_MINES 10
#define CELL_SIZE 24
#define GRID_OX   12
#define GRID_OY   8

/* Cell states */
#define CELL_HIDDEN   0
#define CELL_REVEALED 1
#define CELL_FLAGGED  2

/* ── State ─────────────────────────────────────────────────────── */

static ui_window_t *mines_win = NULL;
static int grid_widget_idx = -1;
static int mines_label_idx = -1;

static struct {
    uint8_t mine;       /* 1 = mine */
    uint8_t state;      /* CELL_* */
    uint8_t adjacent;   /* 0-8 neighbor mine count */
} cells[GRID_W * GRID_H];

static int game_over;
static int game_won;
static int flags_placed;

/* Simple PRNG using PIT ticks */
static uint32_t ms_seed;
static uint32_t ms_rand(void)
{
    ms_seed = ms_seed * 1103515245 + 12345;
    return (ms_seed >> 16) & 0x7FFF;
}

/* ── Game logic ────────────────────────────────────────────────── */

static int cell_idx(int cx, int cy) { return cy * GRID_W + cx; }

static void count_adjacent(void)
{
    int x, y, dx, dy;
    for (y = 0; y < GRID_H; y++) {
        for (x = 0; x < GRID_W; x++) {
            int count = 0;
            for (dy = -1; dy <= 1; dy++) {
                for (dx = -1; dx <= 1; dx++) {
                    int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && nx < GRID_W && ny >= 0 && ny < GRID_H) {
                        if (cells[cell_idx(nx, ny)].mine)
                            count++;
                    }
                }
            }
            cells[cell_idx(x, y)].adjacent = (uint8_t)count;
        }
    }
}

static void new_game(void)
{
    memset(cells, 0, sizeof(cells));
    game_over = 0;
    game_won = 0;
    flags_placed = 0;
    ms_seed = pit_get_ticks();

    /* Place mines */
    int placed = 0;
    while (placed < NUM_MINES) {
        int pos = (int)(ms_rand() % (GRID_W * GRID_H));
        if (!cells[pos].mine) {
            cells[pos].mine = 1;
            placed++;
        }
    }
    count_adjacent();

    if (mines_win && mines_label_idx >= 0) {
        ui_widget_t *w = ui_get_widget(mines_win, mines_label_idx);
        if (w) snprintf(w->label.text, UI_TEXT_MAX, "Mines: %d",
                        NUM_MINES - flags_placed);
        mines_win->dirty = 1;
    }
}

static void flood_reveal(int x, int y)
{
    if (x < 0 || x >= GRID_W || y < 0 || y >= GRID_H) return;
    int idx = cell_idx(x, y);
    if (cells[idx].state != CELL_HIDDEN) return;
    if (cells[idx].mine) return;

    cells[idx].state = CELL_REVEALED;

    if (cells[idx].adjacent == 0) {
        int dx, dy;
        for (dy = -1; dy <= 1; dy++)
            for (dx = -1; dx <= 1; dx++)
                if (dx || dy)
                    flood_reveal(x + dx, y + dy);
    }
}

static void check_win(void)
{
    int x, y;
    for (y = 0; y < GRID_H; y++)
        for (x = 0; x < GRID_W; x++)
            if (!cells[cell_idx(x, y)].mine &&
                cells[cell_idx(x, y)].state != CELL_REVEALED)
                return;
    game_won = 1;
    game_over = 1;
}

static void reveal_cell(int x, int y)
{
    if (game_over) return;
    int idx = cell_idx(x, y);
    if (cells[idx].state != CELL_HIDDEN) return;

    if (cells[idx].mine) {
        /* Game over — reveal all mines */
        game_over = 1;
        int i;
        for (i = 0; i < GRID_W * GRID_H; i++)
            if (cells[i].mine) cells[i].state = CELL_REVEALED;
        return;
    }

    flood_reveal(x, y);
    check_win();
}

static void toggle_flag(int x, int y)
{
    if (game_over) return;
    int idx = cell_idx(x, y);
    if (cells[idx].state == CELL_HIDDEN) {
        cells[idx].state = CELL_FLAGGED;
        flags_placed++;
    } else if (cells[idx].state == CELL_FLAGGED) {
        cells[idx].state = CELL_HIDDEN;
        flags_placed--;
    }

    if (mines_win && mines_label_idx >= 0) {
        ui_widget_t *w = ui_get_widget(mines_win, mines_label_idx);
        if (w) snprintf(w->label.text, UI_TEXT_MAX, "Mines: %d",
                        NUM_MINES - flags_placed);
    }
}

/* ── Custom widget callbacks ───────────────────────────────────── */

static const uint32_t num_colors[] = {
    0xFF0000FF, /* 1 = blue */
    0xFF008000, /* 2 = green */
    0xFFFF0000, /* 3 = red */
    0xFF000080, /* 4 = dark blue */
    0xFF800000, /* 5 = maroon */
    0xFF008080, /* 6 = teal */
    0xFF000000, /* 7 = black */
    0xFF808080, /* 8 = gray */
};

static void grid_draw(ui_window_t *win, int widget_idx,
                      uint32_t *canvas, int cw, int ch)
{
    (void)win;
    ui_widget_t *wg = ui_get_widget(win, widget_idx);
    if (!wg) return;

    gfx_surface_t gs = { canvas, cw, ch, cw };
    int ox = wg->x, oy = wg->y;

    int x, y;
    for (y = 0; y < GRID_H; y++) {
        for (x = 0; x < GRID_W; x++) {
            int px = ox + x * CELL_SIZE;
            int py = oy + y * CELL_SIZE;
            int idx = cell_idx(x, y);

            if (cells[idx].state == CELL_HIDDEN) {
                gfx_surf_fill_rect(&gs, px, py, CELL_SIZE - 1,
                                   CELL_SIZE - 1, 0xFF585B70);
                gfx_surf_draw_rect(&gs, px, py, CELL_SIZE - 1,
                                   CELL_SIZE - 1, 0xFF45475A);
            } else if (cells[idx].state == CELL_FLAGGED) {
                gfx_surf_fill_rect(&gs, px, py, CELL_SIZE - 1,
                                   CELL_SIZE - 1, 0xFF585B70);
                gfx_surf_draw_rect(&gs, px, py, CELL_SIZE - 1,
                                   CELL_SIZE - 1, 0xFF45475A);
                /* Flag symbol: "F" */
                gfx_surf_draw_char_smooth(&gs, px + 8, py + 4,
                                          'F', 0xFFF38BA8, 1);
            } else {
                /* Revealed */
                if (cells[idx].mine) {
                    gfx_surf_fill_rect(&gs, px, py, CELL_SIZE - 1,
                                       CELL_SIZE - 1, 0xFFF38BA8);
                    gfx_surf_draw_char_smooth(&gs, px + 8, py + 4,
                                              '*', 0xFF1E1E2E, 1);
                } else {
                    gfx_surf_fill_rect(&gs, px, py, CELL_SIZE - 1,
                                       CELL_SIZE - 1, 0xFF313244);
                    if (cells[idx].adjacent > 0) {
                        char ch_digit = '0' + cells[idx].adjacent;
                        uint32_t col = num_colors[cells[idx].adjacent - 1];
                        gfx_surf_draw_char_smooth(&gs, px + 8, py + 4,
                                                  ch_digit, col, 1);
                    }
                }
            }
        }
    }

    /* Status overlay */
    if (game_over) {
        const char *msg = game_won ? "You Win!" : "Game Over";
        uint32_t col = game_won ? 0xFFA6E3A1 : 0xFFF38BA8;
        int tw = (int)strlen(msg) * 8;
        int mx = ox + (GRID_W * CELL_SIZE - tw) / 2;
        int my = oy + (GRID_H * CELL_SIZE) / 2 - 8;
        /* Dark background for readability */
        gfx_surf_fill_rect_alpha(&gs, mx - 4, my - 2, tw + 8, 20,
                                 0xFF000000, 192);
        gfx_surf_draw_string_smooth(&gs, mx, my, msg, col, 1);
    }
}

static int grid_event(ui_window_t *win, int widget_idx, ui_event_t *ev)
{
    ui_widget_t *wg = ui_get_widget(win, widget_idx);
    if (!wg) return 0;

    if (ev->type == UI_EVENT_MOUSE_DOWN) {
        int lx = ev->mouse.wx - wg->x;
        int ly = ev->mouse.wy - wg->y;
        int cx = lx / CELL_SIZE;
        int cy = ly / CELL_SIZE;

        if (cx >= 0 && cx < GRID_W && cy >= 0 && cy < GRID_H) {
            if (ev->mouse.buttons & 2) {
                toggle_flag(cx, cy);
            } else {
                reveal_cell(cx, cy);
            }
            win->dirty = 1;
            return 1;
        }
    }
    return 0;
}

/* ── Button callbacks ──────────────────────────────────────────── */

static void cb_new_game(ui_window_t *win, int idx)
{
    (void)idx;
    new_game();
    win->dirty = 1;
}

/* ── Public API ────────────────────────────────────────────────── */

void app_minesweeper_open(void)
{
    if (mines_win) {
        ui_window_focus(mines_win->wm_id);
        ui_window_raise(mines_win->wm_id);
        return;
    }

    int grid_px_w = GRID_W * CELL_SIZE;
    int grid_px_h = GRID_H * CELL_SIZE;
    int ww = grid_px_w + 24;
    int wh = grid_px_h + GRID_OY + 44;
    int sw = (int)gfx_width(), sh = (int)gfx_height();

    mines_win = uw_create(sw / 2 - ww / 2 + 80, sh / 2 - wh / 2,
                          ww, wh, "Minesweeper");
    if (!mines_win) return;

    /* Header: mine count + new game button */
    mines_label_idx = ui_add_label(mines_win, 12, GRID_OY, 100, 20,
                                   "Mines: 10", ui_theme.text_primary);
    ui_add_button(mines_win, ww - 100, GRID_OY - 2, 80, 24,
                  "New Game", cb_new_game);

    /* Grid custom widget */
    grid_widget_idx = ui_add_custom(mines_win, GRID_OX, GRID_OY + 28,
                                    grid_px_w, grid_px_h,
                                    grid_draw, grid_event, NULL);

    new_game();
    uw_redraw(mines_win);
}

int minesweeper_tick(int mx, int my, int btn_down, int btn_up)
{
    if (!mines_win) return 0;
    int r = uw_tick(mines_win, mx, my, btn_down, btn_up, 0);
    if (mines_win && mines_win->wm_id < 0) {
        mines_win = NULL;
        grid_widget_idx = -1;
        mines_label_idx = -1;
    }
    return r;
}

int minesweeper_win_open(void) { return mines_win != NULL; }
