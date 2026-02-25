/* dock.c -- bottom dock bar with app icons
 *
 * Single compositor surface on COMP_LAYER_OVERLAY, centered at screen
 * bottom with a gap. Contains clickable app icons with labels.
 * Phase 4: static icons, click detection, no magnification yet.
 */
#include <kernel/dock.h>
#include <kernel/compositor.h>
#include <kernel/gfx.h>
#include <kernel/ui_theme.h>
#include <string.h>

/* ── Dock items ───────────────────────────────────────────────────── */

typedef struct {
    const char *label;    /* short name shown below icon */
    uint32_t   color;     /* icon fill color */
    int        running;   /* 1 = has open window */
} dock_item_t;

enum {
    DOCK_ACT_NONE = -1,
    DOCK_ACT_TERMINAL = 0,
    DOCK_ACT_FILES,
    DOCK_ACT_SETTINGS,
    DOCK_ACT_MONITOR,
    DOCK_ACT_COUNT
};

static dock_item_t items[] = {
    { "Term",     0xFF89B4FA, 0 },  /* blue - terminal */
    { "Files",    0xFFA6E3A1, 0 },  /* green - file manager */
    { "Settings", 0xFFF9E2AF, 0 },  /* yellow - settings */
    { "Monitor",  0xFFF38BA8, 0 },  /* red - system monitor */
};

#define ITEM_COUNT ((int)(sizeof(items) / sizeof(items[0])))

/* ── Surface state ────────────────────────────────────────────────── */

static comp_surface_t *dock_surf = 0;
static int dock_w = 0, dock_h = 0;
static int dock_screen_x = 0, dock_screen_y = 0;
static int hover_idx = -1;
static int pending_action = DOCK_ACT_NONE;

/* ── Geometry ─────────────────────────────────────────────────────── */

/* Total dock width: padding + (icon+gap)*N - gap + padding */
static int calc_dock_width(void) {
    return DOCK_PADDING * 2 +
           ITEM_COUNT * DOCK_ICON_SIZE +
           (ITEM_COUNT - 1) * DOCK_ITEM_GAP;
}

/* Icon rect within the dock surface (local coords) */
static void item_rect(int idx, int *ox, int *oy, int *ow, int *oh) {
    *ox = DOCK_PADDING + idx * (DOCK_ICON_SIZE + DOCK_ITEM_GAP);
    *oy = DOCK_PADDING;
    *ow = DOCK_ICON_SIZE;
    *oh = DOCK_ICON_SIZE;
}

/* ── Paint ────────────────────────────────────────────────────────── */

static void draw_rounded_rect_fill(uint32_t *px, int pitch, int x0, int y0,
                                   int w, int h, int r, uint32_t color) {
    for (int y = y0; y < y0 + h; y++) {
        for (int x = x0; x < x0 + w; x++) {
            /* Check corners */
            int dx = 0, dy = 0, inside = 1;
            if (x < x0 + r && y < y0 + r) { dx = x - (x0 + r); dy = y - (y0 + r); }
            else if (x >= x0 + w - r && y < y0 + r) { dx = x - (x0 + w - r - 1); dy = y - (y0 + r); }
            else if (x < x0 + r && y >= y0 + h - r) { dx = x - (x0 + r); dy = y - (y0 + h - r - 1); }
            else if (x >= x0 + w - r && y >= y0 + h - r) { dx = x - (x0 + w - r - 1); dy = y - (y0 + h - r - 1); }
            if (dx != 0 || dy != 0) inside = (dx * dx + dy * dy <= r * r);
            if (inside) px[y * pitch + x] = color;
        }
    }
}

void dock_paint(void) {
    if (!dock_surf) return;

    int w = dock_w, h = dock_h;
    uint32_t *px = dock_surf->pixels;

    /* Clear to transparent */
    memset(px, 0, (size_t)w * h * 4);

    /* Rounded background pill */
    uint32_t pill_bg = 0xDC181825; /* alpha=220, Catppuccin mantle */
    draw_rounded_rect_fill(px, w, 0, 0, w, h, 12, pill_bg);

    /* 1px top border highlight */
    for (int x = 12; x < w - 12; x++)
        px[x] = 0x40585B70; /* subtle light border */

    /* Icons */
    for (int i = 0; i < ITEM_COUNT; i++) {
        int ix, iy, iw, ih;
        item_rect(i, &ix, &iy, &iw, &ih);

        uint32_t col = items[i].color;

        /* Hover highlight */
        if (i == hover_idx) {
            draw_rounded_rect_fill(px, w, ix - 2, iy - 2, iw + 4, ih + 4,
                                   8, 0x30FFFFFF);
        }

        /* Icon: rounded square with first letter */
        draw_rounded_rect_fill(px, w, ix, iy, iw, ih, 8, col);

        /* Draw first letter of label centered in icon */
        {
            char letter[2] = { items[i].label[0], '\0' };
            int lx = ix + (iw - 8) / 2;
            int ly = iy + (ih - 16) / 2;
            gfx_surface_t gs;
            gs.buf = px; gs.w = w; gs.h = h; gs.pitch = w;
            gfx_surf_draw_string(&gs, lx, ly, letter, 0xFF11111B, col);
        }

        /* Running indicator dot */
        if (items[i].running) {
            int cx = ix + iw / 2;
            int cy = iy + ih + 5;
            for (int dy = -2; dy <= 2; dy++)
                for (int dx = -2; dx <= 2; dx++)
                    if (dx*dx + dy*dy <= 4 && cy+dy >= 0 && cy+dy < h && cx+dx >= 0 && cx+dx < w)
                        px[(cy+dy)*w + (cx+dx)] = 0xFFCDD6F4;
        }
    }

    comp_surface_damage_all(dock_surf);
}

/* ── Init ─────────────────────────────────────────────────────────── */

void dock_init(void) {
    int sw = (int)gfx_width();
    int sh = (int)gfx_height();

    dock_w = calc_dock_width();
    dock_h = DOCK_ICON_SIZE + DOCK_PADDING * 2 + 10; /* extra for dot indicator */

    dock_screen_x = (sw - dock_w) / 2;
    dock_screen_y = sh - dock_h - DOCK_GAP;

    dock_surf = comp_surface_create(dock_w, dock_h, COMP_LAYER_OVERLAY);
    if (!dock_surf) return;

    comp_surface_move(dock_surf, dock_screen_x, dock_screen_y);
    dock_paint();
}

/* ── Mouse ────────────────────────────────────────────────────────── */

int dock_mouse(int mx, int my, int down, int up) {
    if (!dock_surf) return 0;

    /* Convert to dock-local coords */
    int lx = mx - dock_screen_x;
    int ly = my - dock_screen_y;

    /* Outside dock? */
    if (lx < 0 || lx >= dock_w || ly < 0 || ly >= dock_h) {
        if (hover_idx >= 0) {
            hover_idx = -1;
            dock_paint();
        }
        return 0;
    }

    /* Hit-test icons */
    int new_hover = -1;
    for (int i = 0; i < ITEM_COUNT; i++) {
        int ix, iy, iw, ih;
        item_rect(i, &ix, &iy, &iw, &ih);
        if (lx >= ix && lx < ix + iw && ly >= iy && ly < iy + ih) {
            new_hover = i;
            break;
        }
    }

    if (new_hover != hover_idx) {
        hover_idx = new_hover;
        dock_paint();
    }

    /* Click */
    if (up && hover_idx >= 0) {
        pending_action = hover_idx;
    }

    return 1; /* consumed */
}

int dock_consume_action(void) {
    int a = pending_action;
    pending_action = DOCK_ACT_NONE;
    return a;
}
