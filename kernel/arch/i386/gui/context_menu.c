/* context_menu.c — Right-click context menu on the desktop.
 *
 * Shows a frosted-glass popup with standard desktop actions.
 * Items: Change Wallpaper, Display Settings, About ImposOS,
 *        and "Show [App]" for each minimized window.
 */
#include <kernel/context_menu.h>
#include <kernel/compositor.h>
#include <kernel/gfx.h>
#include <kernel/wm2.h>
#include <kernel/app.h>
#include <string.h>

/* ── Layout ─────────────────────────────────────────────────────── */
#define MENU_W      200
#define ITEM_H       28
#define ITEM_PAD_X    7
#define ITEM_PAD_Y    4
#define SEP_H         7
#define CORNER_R      8
#define MAX_ITEMS    16

/* ── Menu item types ────────────────────────────────────────────── */
#define ITEM_ACTION    0
#define ITEM_SEPARATOR 1

typedef struct {
    int         type;
    const char *label;
    int         action;   /* CTX_ACTION_* */
    int         win_id;   /* for SHOW_WIN actions */
} menu_item_t;

/* Actions */
#define CTX_WALLPAPER    0
#define CTX_DISPLAY      1
#define CTX_ABOUT        2
#define CTX_SHOW_WIN     3

/* ── State ──────────────────────────────────────────────────────── */
static comp_surface_t *surf = 0;
static int vis = 0;
static int menu_x = 0, menu_y = 0;
static int menu_h = 0;
static int hover_item = -1;

static menu_item_t items[MAX_ITEMS];
static int item_count = 0;

/* ── Build item list ────────────────────────────────────────────── */

/* Static storage for minimized window labels */
static char win_labels[8][72];

static void build_menu(void) {
    item_count = 0;

    /* Fixed items */
    items[item_count].type = ITEM_ACTION;
    items[item_count].label = "Change Wallpaper";
    items[item_count].action = CTX_WALLPAPER;
    items[item_count].win_id = -1;
    item_count++;

    items[item_count].type = ITEM_ACTION;
    items[item_count].label = "Display Settings";
    items[item_count].action = CTX_DISPLAY;
    items[item_count].win_id = -1;
    item_count++;

    items[item_count].type = ITEM_SEPARATOR;
    items[item_count].label = 0;
    items[item_count].action = 0;
    items[item_count].win_id = -1;
    item_count++;

    items[item_count].type = ITEM_ACTION;
    items[item_count].label = "About ImposOS";
    items[item_count].action = CTX_ABOUT;
    items[item_count].win_id = -1;
    item_count++;

    /* Minimized windows: try IDs 1..64 */
    int added_sep = 0;
    int win_label_idx = 0;
    for (int wid = 1; wid <= 64 && item_count < MAX_ITEMS - 1; wid++) {
        wm2_info_t info = wm2_get_info(wid);
        if (info.id <= 0 || info.state != WM2_STATE_MINIMIZED) continue;
        if (!info.title[0]) continue;
        if (!added_sep) {
            items[item_count].type = ITEM_SEPARATOR;
            items[item_count].label = 0;
            items[item_count].action = 0;
            items[item_count].win_id = -1;
            item_count++;
            added_sep = 1;
        }
        /* Build "Show [Title]" label in static buffer */
        char *lbl = win_labels[win_label_idx];
        lbl[0] = 'S'; lbl[1] = 'h'; lbl[2] = 'o'; lbl[3] = 'w'; lbl[4] = ' ';
        int j = 5;
        for (int k = 0; info.title[k] && j < 70; k++, j++)
            lbl[j] = info.title[k];
        lbl[j] = '\0';

        items[item_count].type = ITEM_ACTION;
        items[item_count].label = lbl;
        items[item_count].action = CTX_SHOW_WIN;
        items[item_count].win_id = wid;
        item_count++;
        win_label_idx++;
        if (win_label_idx >= 8) break;
    }

    /* Calculate menu height */
    menu_h = 0;
    for (int i = 0; i < item_count; i++) {
        menu_h += (items[i].type == ITEM_SEPARATOR) ? SEP_H : ITEM_H;
    }
    menu_h += ITEM_PAD_Y * 2;
}

/* ── Drawing ────────────────────────────────────────────────────── */

static void draw_rrect_fill(uint32_t *px, int pw, int x, int y,
                             int w, int h, int r, uint32_t color) {
    for (int row = y; row < y + h; row++) {
        if (row < 0 || row >= (int)surf->h) continue;
        for (int col = x; col < x + w; col++) {
            if (col < 0 || col >= (int)surf->w) continue;
            int dx = 0, dy = 0, inside = 1;
            if (col<x+r && row<y+r) { dx=col-(x+r); dy=row-(y+r); }
            else if (col>=x+w-r && row<y+r) { dx=col-(x+w-r-1); dy=row-(y+r); }
            else if (col<x+r && row>=y+h-r) { dx=col-(x+r); dy=row-(y+h-r-1); }
            else if (col>=x+w-r && row>=y+h-r) { dx=col-(x+w-r-1); dy=row-(y+h-r-1); }
            if (dx||dy) inside=(dx*dx+dy*dy<=r*r);
            if (inside) px[row*pw+col] = color;
        }
    }
}

void ctx_menu_paint(void) {
    if (!surf || !vis) return;

    int sw = surf->w, sh = surf->h;
    uint32_t *px = surf->pixels;

    /* Clear to transparent */
    memset(px, 0, (size_t)sw * sh * 4);

    /* Shadow (slightly larger rect, semi-transparent dark) */
    draw_rrect_fill(px, sw, menu_x + 4, menu_y + 8,
                    MENU_W + 4, menu_h + 4, CORNER_R, 0x44000000);

    /* Menu background */
    draw_rrect_fill(px, sw, menu_x, menu_y,
                    MENU_W, menu_h, CORNER_R, 0xE0141C28);

    /* Border */
    gfx_surface_t gs;
    gs.buf = px; gs.w = sw; gs.h = sh; gs.pitch = sw;

    /* Draw items */
    int iy = menu_y + ITEM_PAD_Y;
    for (int i = 0; i < item_count; i++) {
        if (items[i].type == ITEM_SEPARATOR) {
            /* Separator line */
            int sy = iy + SEP_H / 2;
            for (int col = menu_x + 8; col < menu_x + MENU_W - 8; col++) {
                if (sy >= 0 && sy < sh && col >= 0 && col < sw)
                    px[sy * sw + col] = 0x28FFFFFF;
            }
            iy += SEP_H;
            continue;
        }

        /* Hover highlight */
        if (i == hover_item) {
            draw_rrect_fill(px, sw, menu_x + 4, iy,
                            MENU_W - 8, ITEM_H, 4, 0x733478F6);
        }

        /* Label */
        const char *label = items[i].label;
        uint32_t fg = (i == hover_item) ? 0xFFFFFFFF : 0xFFCDD6F4;
        if (label) {
            gfx_surf_draw_string(&gs, menu_x + ITEM_PAD_X + 8,
                                 iy + (ITEM_H - 16) / 2,
                                 label, fg, 0);
        }

        iy += ITEM_H;
    }

    comp_surface_damage_all(surf);
}

/* ── Public API ─────────────────────────────────────────────────── */

void ctx_menu_init(void) {
    int sw = (int)gfx_width(), sh = (int)gfx_height();
    surf = comp_surface_create(sw, sh, COMP_LAYER_OVERLAY);
    if (surf) {
        comp_surface_set_visible(surf, 0);
        comp_surface_raise(surf);
    }
    vis = 0;
}

void ctx_menu_show(int x, int y) {
    if (!surf) return;
    build_menu();

    /* Clamp to screen */
    int sw = (int)gfx_width(), sh = (int)gfx_height();
    menu_x = x;
    menu_y = y;
    if (menu_x + MENU_W > sw) menu_x = sw - MENU_W - 8;
    if (menu_y + menu_h > sh) menu_y = sh - menu_h - 8;
    if (menu_x < 0) menu_x = 0;
    if (menu_y < 0) menu_y = 0;

    hover_item = -1;
    vis = 1;
    comp_surface_set_visible(surf, 1);
    comp_surface_raise(surf);
    ctx_menu_paint();
}

void ctx_menu_hide(void) {
    if (!surf) return;
    vis = 0;
    comp_surface_set_visible(surf, 0);
    comp_surface_damage_all(surf);
}

int ctx_menu_visible(void) { return vis; }

int ctx_menu_mouse(int mx, int my, int btn_down, int btn_up) {
    if (!vis || !surf) return 0;
    (void)btn_down;

    /* Check if inside menu */
    if (mx < menu_x || mx >= menu_x + MENU_W ||
        my < menu_y || my >= menu_y + menu_h) {
        if (btn_up) ctx_menu_hide();
        return 0;
    }

    /* Find hovered item */
    int iy = menu_y + ITEM_PAD_Y;
    int new_hover = -1;
    for (int i = 0; i < item_count; i++) {
        if (items[i].type == ITEM_SEPARATOR) { iy += SEP_H; continue; }
        if (my >= iy && my < iy + ITEM_H) { new_hover = i; break; }
        iy += ITEM_H;
    }

    int needs_repaint = (new_hover != hover_item);
    hover_item = new_hover;
    if (needs_repaint) ctx_menu_paint();

    if (btn_up && hover_item >= 0 &&
        items[hover_item].type == ITEM_ACTION) {
        int action = items[hover_item].action;
        int wid = items[hover_item].win_id;
        ctx_menu_hide();

        switch (action) {
        case CTX_WALLPAPER:
        case CTX_DISPLAY:
        case CTX_ABOUT:
            /* Open settings to appropriate tab */
            app_launch("settings");
            break;
        case CTX_SHOW_WIN:
            if (wid >= 0) wm2_restore(wid);
            break;
        }
        return 1;
    }

    return 1;
}
