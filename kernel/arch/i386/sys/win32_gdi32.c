#include <kernel/win32_types.h>
#include <kernel/wm.h>
#include <kernel/gfx.h>
#include <kernel/task.h>
#include <kernel/io.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../gui/font8x16.h"

/* ── DC (Device Context) Management ──────────────────────────── */
#define MAX_DCS 32
#define DC_SAVE_STACK_DEPTH 8

typedef struct {
    COLORREF text_color;
    COLORREF bg_color;
    int bg_mode;
    HBRUSH current_brush;
    HFONT current_font;
    HPEN current_pen;
    int cur_x, cur_y;
    int viewport_org_x, viewport_org_y;
    int window_org_x, window_org_y;
    int clip_left, clip_top, clip_right, clip_bottom;
    int has_clip;
} dc_saved_state_t;

typedef struct {
    int in_use;
    int wm_id;         /* WM window this DC is bound to */
    HWND hwnd;
    COLORREF text_color;
    COLORREF bg_color;
    int bg_mode;        /* TRANSPARENT or OPAQUE */
    HBRUSH current_brush;
    HFONT current_font;
    HPEN current_pen;
    /* Paint state */
    int is_paint_dc;    /* allocated via BeginPaint */
    /* Current position for MoveToEx/LineTo */
    int cur_x, cur_y;
    /* Viewport/window origins for coordinate transforms */
    int viewport_org_x, viewport_org_y;
    int window_org_x, window_org_y;
    /* Clipping rectangle */
    int clip_left, clip_top, clip_right, clip_bottom;
    int has_clip;
    /* Memory DC support */
    int is_memory_dc;
    uint32_t *mem_buf;
    int mem_w, mem_h;
    /* SaveDC/RestoreDC stack */
    dc_saved_state_t save_stack[DC_SAVE_STACK_DEPTH];
    int save_level;
} dc_state_t;

static dc_state_t dc_table[MAX_DCS];

/* ── GDI Object (Brush/Font/Pen/Bitmap/Region) Table ─────────── */
#define MAX_GDI_OBJECTS 64

typedef enum {
    GDI_OBJ_FREE = 0,
    GDI_OBJ_BRUSH,
    GDI_OBJ_FONT,
    GDI_OBJ_PEN,
    GDI_OBJ_BITMAP,
    GDI_OBJ_REGION,
} gdi_obj_type_t;

typedef struct {
    gdi_obj_type_t type;
    union {
        COLORREF brush_color;
        struct {
            int height;
            int weight;
            char face_name[32];
        } font;
        struct {
            COLORREF color;
            int width;
            int style;
        } pen;
        struct {
            uint32_t *bits;
            int width;
            int height;
            int bpp;
        } bitmap;
        struct {
            int left, top, right, bottom;
        } region;
    };
} gdi_object_t;

static gdi_object_t gdi_objects[MAX_GDI_OBJECTS];

/* Stock objects (pre-allocated) */
#define STOCK_WHITE_BRUSH    1
#define STOCK_BLACK_BRUSH    2
#define STOCK_NULL_BRUSH     3
#define STOCK_SYSTEM_FONT    4
#define STOCK_WHITE_PEN      5
#define STOCK_BLACK_PEN      6
#define STOCK_NULL_PEN       7

static void init_stock_objects(void) {
    static int initialized = 0;
    if (initialized) return;
    initialized = 1;

    gdi_objects[STOCK_WHITE_BRUSH].type = GDI_OBJ_BRUSH;
    gdi_objects[STOCK_WHITE_BRUSH].brush_color = RGB(255, 255, 255);

    gdi_objects[STOCK_BLACK_BRUSH].type = GDI_OBJ_BRUSH;
    gdi_objects[STOCK_BLACK_BRUSH].brush_color = RGB(0, 0, 0);

    gdi_objects[STOCK_NULL_BRUSH].type = GDI_OBJ_BRUSH;
    gdi_objects[STOCK_NULL_BRUSH].brush_color = 0;  /* transparent */

    gdi_objects[STOCK_SYSTEM_FONT].type = GDI_OBJ_FONT;
    gdi_objects[STOCK_SYSTEM_FONT].font.height = 16;
    gdi_objects[STOCK_SYSTEM_FONT].font.weight = 400;
    strcpy(gdi_objects[STOCK_SYSTEM_FONT].font.face_name, "System");

    gdi_objects[STOCK_WHITE_PEN].type = GDI_OBJ_PEN;
    gdi_objects[STOCK_WHITE_PEN].pen.color = RGB(255, 255, 255);
    gdi_objects[STOCK_WHITE_PEN].pen.width = 1;
    gdi_objects[STOCK_WHITE_PEN].pen.style = PS_SOLID;

    gdi_objects[STOCK_BLACK_PEN].type = GDI_OBJ_PEN;
    gdi_objects[STOCK_BLACK_PEN].pen.color = RGB(0, 0, 0);
    gdi_objects[STOCK_BLACK_PEN].pen.width = 1;
    gdi_objects[STOCK_BLACK_PEN].pen.style = PS_SOLID;

    gdi_objects[STOCK_NULL_PEN].type = GDI_OBJ_PEN;
    gdi_objects[STOCK_NULL_PEN].pen.color = 0;
    gdi_objects[STOCK_NULL_PEN].pen.width = 0;
    gdi_objects[STOCK_NULL_PEN].pen.style = PS_NULL;
}

static HGDIOBJ alloc_gdi_obj(gdi_obj_type_t type) {
    init_stock_objects();
    for (int i = 10; i < MAX_GDI_OBJECTS; i++) {  /* skip stock objects */
        if (gdi_objects[i].type == GDI_OBJ_FREE) {
            memset(&gdi_objects[i], 0, sizeof(gdi_objects[i]));
            gdi_objects[i].type = type;
            return (HGDIOBJ)(i + 1);
        }
    }
    return (HGDIOBJ)0;
}

static gdi_object_t *get_gdi_obj(HGDIOBJ h) {
    int idx = (int)h - 1;
    if (idx < 0 || idx >= MAX_GDI_OBJECTS) return NULL;
    if (gdi_objects[idx].type == GDI_OBJ_FREE) return NULL;
    return &gdi_objects[idx];
}

/* ── COLORREF → GFX_RGB conversion ──────────────────────────── */
static uint32_t colorref_to_gfx(COLORREF c) {
    return GFX_RGB(GetRValue(c), GetGValue(c), GetBValue(c));
}

/* ── Canvas helper: works for both window DCs and memory DCs ── */
static uint32_t *dc_get_canvas(dc_state_t *dc, int *out_w, int *out_h) {
    if (dc->is_memory_dc) {
        *out_w = dc->mem_w;
        *out_h = dc->mem_h;
        return dc->mem_buf;
    }
    return wm_get_canvas(dc->wm_id, out_w, out_h);
}

/* ── Transform helper: apply viewport/window origins ─────────── */
static inline int tx(dc_state_t *dc, int x) {
    return x + dc->viewport_org_x - dc->window_org_x;
}
static inline int ty(dc_state_t *dc, int y) {
    return y + dc->viewport_org_y - dc->window_org_y;
}

/* ── Resolve HWND to WM id (from user32's window table) ────── */

static int hwnd_to_wm_id(HWND hwnd);

/* Simple HWND registry shared with user32 — we store HWND→wm_id here */
#define MAX_HWND_MAP 16
static struct { HWND hwnd; int wm_id; } hwnd_map[MAX_HWND_MAP];

void win32_gdi_register_hwnd(HWND hwnd, int wm_id) {
    for (int i = 0; i < MAX_HWND_MAP; i++) {
        if (hwnd_map[i].hwnd == 0) {
            hwnd_map[i].hwnd = hwnd;
            hwnd_map[i].wm_id = wm_id;
            return;
        }
    }
}

void win32_gdi_unregister_hwnd(HWND hwnd) {
    for (int i = 0; i < MAX_HWND_MAP; i++) {
        if (hwnd_map[i].hwnd == hwnd) {
            hwnd_map[i].hwnd = 0;
            hwnd_map[i].wm_id = 0;
            return;
        }
    }
}

static int hwnd_to_wm_id(HWND hwnd) {
    for (int i = 0; i < MAX_HWND_MAP; i++) {
        if (hwnd_map[i].hwnd == hwnd)
            return hwnd_map[i].wm_id;
    }
    /* Fallback: find a WM window belonging to current task */
    int tid = task_get_current();
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        wm_window_t *w = wm_get_window_by_index(i);
        if (w && w->task_id == tid)
            return w->id;
    }
    return -1;
}

/* ── DC initialization helper ────────────────────────────────── */
static void dc_init_defaults(dc_state_t *dc) {
    dc->text_color = RGB(0, 0, 0);
    dc->bg_color = RGB(255, 255, 255);
    dc->bg_mode = OPAQUE;
    dc->current_brush = 0;
    dc->current_font = 0;
    dc->current_pen = 0;
    dc->is_paint_dc = 0;
    dc->cur_x = 0;
    dc->cur_y = 0;
    dc->viewport_org_x = 0;
    dc->viewport_org_y = 0;
    dc->window_org_x = 0;
    dc->window_org_y = 0;
    dc->clip_left = 0;
    dc->clip_top = 0;
    dc->clip_right = 0;
    dc->clip_bottom = 0;
    dc->has_clip = 0;
    dc->is_memory_dc = 0;
    dc->mem_buf = NULL;
    dc->mem_w = 0;
    dc->mem_h = 0;
    dc->save_level = 0;
}

/* ── GetDC / ReleaseDC ───────────────────────────────────────── */

static HDC WINAPI shim_GetDC(HWND hWnd) {
    init_stock_objects();

    for (int i = 0; i < MAX_DCS; i++) {
        if (!dc_table[i].in_use) {
            dc_table[i].in_use = 1;
            dc_table[i].hwnd = hWnd;
            dc_table[i].wm_id = hwnd_to_wm_id(hWnd);
            dc_init_defaults(&dc_table[i]);
            return (HDC)(i + 1);
        }
    }
    return (HDC)0;
}

static int WINAPI shim_ReleaseDC(HWND hWnd, HDC hDC) {
    (void)hWnd;
    int idx = (int)hDC - 1;
    if (idx >= 0 && idx < MAX_DCS) {
        dc_table[idx].in_use = 0;
    }
    return 1;
}

/* ── BeginPaint / EndPaint ───────────────────────────────────── */

static HDC WINAPI shim_BeginPaint(HWND hWnd, LPPAINTSTRUCT lpPaint) {
    HDC hdc = shim_GetDC(hWnd);

    if (lpPaint) {
        memset(lpPaint, 0, sizeof(PAINTSTRUCT));
        lpPaint->hdc = hdc;
        lpPaint->fErase = TRUE;

        int idx = (int)hdc - 1;
        if (idx >= 0 && idx < MAX_DCS) {
            dc_table[idx].is_paint_dc = 1;

            /* Fill rcPaint with client area */
            int cx, cy, cw, ch;
            wm_get_content_rect(dc_table[idx].wm_id, &cx, &cy, &cw, &ch);
            lpPaint->rcPaint.left = 0;
            lpPaint->rcPaint.top = 0;
            lpPaint->rcPaint.right = cw;
            lpPaint->rcPaint.bottom = ch;
        }
    }

    return hdc;
}

static BOOL WINAPI shim_EndPaint(HWND hWnd, const PAINTSTRUCT *lpPaint) {
    (void)hWnd;
    if (lpPaint) {
        shim_ReleaseDC(hWnd, lpPaint->hdc);
    }
    wm_mark_dirty();
    return TRUE;
}

/* ── Drawing Functions ───────────────────────────────────────── */

static BOOL WINAPI shim_TextOutA(HDC hdc, int x, int y, LPCSTR lpString, int c) {
    int idx = (int)hdc - 1;
    if (idx < 0 || idx >= MAX_DCS || !dc_table[idx].in_use) return FALSE;

    dc_state_t *dc = &dc_table[idx];
    int dx = tx(dc, x);
    int dy = ty(dc, y);
    uint32_t fg = colorref_to_gfx(dc->text_color);
    uint32_t bg = (dc->bg_mode == TRANSPARENT) ? 0 : colorref_to_gfx(dc->bg_color);

    if (dc->is_memory_dc) {
        /* Draw to memory buffer */
        if (!dc->mem_buf) return FALSE;
        for (int i = 0; i < c && lpString[i]; i++) {
            unsigned char ch = (unsigned char)lpString[i];
            for (int row = 0; row < 16; row++) {
                uint8_t bits = font8x16[ch][row];
                for (int col = 0; col < 8; col++) {
                    int px = dx + i * 8 + col;
                    int py = dy + row;
                    if (px >= 0 && px < dc->mem_w && py >= 0 && py < dc->mem_h) {
                        if (bits & (0x80 >> col))
                            dc->mem_buf[py * dc->mem_w + px] = fg;
                        else if (dc->bg_mode == OPAQUE)
                            dc->mem_buf[py * dc->mem_w + px] = bg;
                    }
                }
            }
        }
    } else {
        for (int i = 0; i < c && lpString[i]; i++) {
            if (dc->bg_mode == OPAQUE) {
                wm_fill_rect(dc->wm_id, dx + i * 8, dy, 8, 16, bg);
            }
            wm_draw_char(dc->wm_id, dx + i * 8, dy, lpString[i], fg,
                          dc->bg_mode == TRANSPARENT ? 0x00000000 : bg);
        }
        wm_mark_dirty();
    }

    return TRUE;
}

static COLORREF WINAPI shim_SetPixel(HDC hdc, int x, int y, COLORREF color) {
    int idx = (int)hdc - 1;
    if (idx < 0 || idx >= MAX_DCS || !dc_table[idx].in_use) return 0;

    dc_state_t *dc = &dc_table[idx];
    int dx = tx(dc, x);
    int dy = ty(dc, y);

    if (dc->is_memory_dc) {
        if (dc->mem_buf && dx >= 0 && dx < dc->mem_w && dy >= 0 && dy < dc->mem_h)
            dc->mem_buf[dy * dc->mem_w + dx] = colorref_to_gfx(color);
    } else {
        wm_put_pixel(dc->wm_id, dx, dy, colorref_to_gfx(color));
    }
    return color;
}

static COLORREF WINAPI shim_GetPixel(HDC hdc, int x, int y) {
    int idx = (int)hdc - 1;
    if (idx < 0 || idx >= MAX_DCS || !dc_table[idx].in_use) return 0;

    dc_state_t *dc = &dc_table[idx];
    int dx = tx(dc, x);
    int dy = ty(dc, y);

    int cw, ch;
    uint32_t *canvas = dc_get_canvas(dc, &cw, &ch);
    if (!canvas || dx < 0 || dy < 0 || dx >= cw || dy >= ch) return 0;

    uint32_t pixel = canvas[dy * cw + dx];
    return RGB((pixel >> 16) & 0xFF, (pixel >> 8) & 0xFF, pixel & 0xFF);
}

/* ── Brush / Object Functions ────────────────────────────────── */

static HBRUSH WINAPI shim_CreateSolidBrush(COLORREF color) {
    HGDIOBJ h = alloc_gdi_obj(GDI_OBJ_BRUSH);
    if (!h) return (HBRUSH)0;
    gdi_object_t *obj = get_gdi_obj(h);
    obj->brush_color = color;
    return (HBRUSH)h;
}

static int WINAPI shim_FillRect(HDC hdc, const RECT *lprc, HBRUSH hbr) {
    int idx = (int)hdc - 1;
    if (idx < 0 || idx >= MAX_DCS || !dc_table[idx].in_use || !lprc) return 0;

    dc_state_t *dc = &dc_table[idx];
    gdi_object_t *brush = get_gdi_obj((HGDIOBJ)hbr);
    uint32_t color;

    if (brush && brush->type == GDI_OBJ_BRUSH) {
        color = colorref_to_gfx(brush->brush_color);
    } else {
        color = GFX_RGB(240, 240, 240);
    }

    int x = tx(dc, lprc->left);
    int y = ty(dc, lprc->top);
    int w = lprc->right - lprc->left;
    int h = lprc->bottom - lprc->top;

    if (dc->is_memory_dc) {
        if (!dc->mem_buf) return 0;
        for (int row = 0; row < h; row++) {
            for (int col = 0; col < w; col++) {
                int px = x + col, py = y + row;
                if (px >= 0 && px < dc->mem_w && py >= 0 && py < dc->mem_h)
                    dc->mem_buf[py * dc->mem_w + px] = color;
            }
        }
    } else {
        wm_fill_rect(dc->wm_id, x, y, w, h, color);
        wm_mark_dirty();
    }
    return 1;
}

static HGDIOBJ WINAPI shim_SelectObject(HDC hdc, HGDIOBJ h) {
    int idx = (int)hdc - 1;
    if (idx < 0 || idx >= MAX_DCS || !dc_table[idx].in_use) return (HGDIOBJ)0;

    gdi_object_t *obj = get_gdi_obj(h);
    if (!obj) return (HGDIOBJ)0;

    HGDIOBJ old = (HGDIOBJ)0;
    switch (obj->type) {
        case GDI_OBJ_BRUSH:
            old = (HGDIOBJ)dc_table[idx].current_brush;
            dc_table[idx].current_brush = (HBRUSH)h;
            break;
        case GDI_OBJ_FONT:
            old = (HGDIOBJ)dc_table[idx].current_font;
            dc_table[idx].current_font = (HFONT)h;
            break;
        case GDI_OBJ_PEN:
            old = (HGDIOBJ)dc_table[idx].current_pen;
            dc_table[idx].current_pen = (HPEN)h;
            break;
        case GDI_OBJ_BITMAP:
            /* Selecting a bitmap into a memory DC resizes the DC buffer */
            if (dc_table[idx].is_memory_dc) {
                dc_table[idx].mem_buf = obj->bitmap.bits;
                dc_table[idx].mem_w = obj->bitmap.width;
                dc_table[idx].mem_h = obj->bitmap.height;
            }
            old = (HGDIOBJ)0;
            break;
        default:
            break;
    }
    return old;
}

static BOOL WINAPI shim_DeleteObject(HGDIOBJ h) {
    int idx = (int)h - 1;
    if (idx < 0 || idx >= MAX_GDI_OBJECTS) return FALSE;
    if (idx < 10) return FALSE;  /* Can't delete stock objects */

    gdi_object_t *obj = &gdi_objects[idx];
    if (obj->type == GDI_OBJ_BITMAP && obj->bitmap.bits) {
        free(obj->bitmap.bits);
        obj->bitmap.bits = NULL;
    }
    obj->type = GDI_OBJ_FREE;
    return TRUE;
}

static HFONT WINAPI shim_CreateFontA(
    int cHeight, int cWidth, int cEscapement, int cOrientation,
    int cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut,
    DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision,
    DWORD iQuality, DWORD iPitchAndFamily, LPCSTR pszFaceName)
{
    (void)cWidth; (void)cEscapement; (void)cOrientation;
    (void)bItalic; (void)bUnderline; (void)bStrikeOut;
    (void)iCharSet; (void)iOutPrecision; (void)iClipPrecision;
    (void)iQuality; (void)iPitchAndFamily;

    HGDIOBJ h = alloc_gdi_obj(GDI_OBJ_FONT);
    if (!h) return (HFONT)0;

    gdi_object_t *obj = get_gdi_obj(h);
    obj->font.height = cHeight > 0 ? cHeight : -cHeight;
    obj->font.weight = cWeight;
    if (pszFaceName)
        strncpy(obj->font.face_name, pszFaceName, 31);

    return (HFONT)h;
}

/* ── Color / Mode ────────────────────────────────────────────── */

static COLORREF WINAPI shim_SetTextColor(HDC hdc, COLORREF color) {
    int idx = (int)hdc - 1;
    if (idx < 0 || idx >= MAX_DCS) return 0;
    COLORREF old = dc_table[idx].text_color;
    dc_table[idx].text_color = color;
    return old;
}

static COLORREF WINAPI shim_SetBkColor(HDC hdc, COLORREF color) {
    int idx = (int)hdc - 1;
    if (idx < 0 || idx >= MAX_DCS) return 0;
    COLORREF old = dc_table[idx].bg_color;
    dc_table[idx].bg_color = color;
    return old;
}

static int WINAPI shim_SetBkMode(HDC hdc, int mode) {
    int idx = (int)hdc - 1;
    if (idx < 0 || idx >= MAX_DCS) return 0;
    int old = dc_table[idx].bg_mode;
    dc_table[idx].bg_mode = mode;
    return old;
}

/* ── Shape Drawing ───────────────────────────────────────────── */

static BOOL WINAPI shim_Rectangle(HDC hdc, int left, int top, int right, int bottom) {
    int idx = (int)hdc - 1;
    if (idx < 0 || idx >= MAX_DCS || !dc_table[idx].in_use) return FALSE;

    dc_state_t *dc = &dc_table[idx];
    int x0 = tx(dc, left), y0 = ty(dc, top);
    int x1 = tx(dc, right), y1 = ty(dc, bottom);

    if (dc->is_memory_dc) {
        if (!dc->mem_buf) return FALSE;
        /* Fill */
        gdi_object_t *brush = get_gdi_obj((HGDIOBJ)dc->current_brush);
        if (brush && brush->type == GDI_OBJ_BRUSH) {
            uint32_t c = colorref_to_gfx(brush->brush_color);
            for (int row = y0; row < y1; row++)
                for (int col = x0; col < x1; col++)
                    if (col >= 0 && col < dc->mem_w && row >= 0 && row < dc->mem_h)
                        dc->mem_buf[row * dc->mem_w + col] = c;
        }
        /* Outline */
        gdi_object_t *pen = get_gdi_obj((HGDIOBJ)dc->current_pen);
        uint32_t outline = pen && pen->type == GDI_OBJ_PEN ? colorref_to_gfx(pen->pen.color) : GFX_RGB(0, 0, 0);
        for (int col = x0; col < x1; col++) {
            if (col >= 0 && col < dc->mem_w) {
                if (y0 >= 0 && y0 < dc->mem_h) dc->mem_buf[y0 * dc->mem_w + col] = outline;
                if (y1 - 1 >= 0 && y1 - 1 < dc->mem_h) dc->mem_buf[(y1 - 1) * dc->mem_w + col] = outline;
            }
        }
        for (int row = y0; row < y1; row++) {
            if (row >= 0 && row < dc->mem_h) {
                if (x0 >= 0 && x0 < dc->mem_w) dc->mem_buf[row * dc->mem_w + x0] = outline;
                if (x1 - 1 >= 0 && x1 - 1 < dc->mem_w) dc->mem_buf[row * dc->mem_w + x1 - 1] = outline;
            }
        }
    } else {
        /* Fill with current brush */
        gdi_object_t *brush = get_gdi_obj((HGDIOBJ)dc->current_brush);
        if (brush && brush->type == GDI_OBJ_BRUSH) {
            wm_fill_rect(dc->wm_id, x0, y0, x1 - x0, y1 - y0,
                          colorref_to_gfx(brush->brush_color));
        }
        /* Outline with current pen */
        gdi_object_t *pen = get_gdi_obj((HGDIOBJ)dc->current_pen);
        uint32_t outline = pen && pen->type == GDI_OBJ_PEN ? colorref_to_gfx(pen->pen.color) : GFX_RGB(0, 0, 0);
        wm_draw_rect(dc->wm_id, x0, y0, x1 - x0, y1 - y0, outline);
        wm_mark_dirty();
    }

    return TRUE;
}

static BOOL WINAPI shim_Ellipse(HDC hdc, int left, int top, int right, int bottom) {
    int idx = (int)hdc - 1;
    if (idx < 0 || idx >= MAX_DCS || !dc_table[idx].in_use) return FALSE;

    dc_state_t *dc = &dc_table[idx];
    int x0 = tx(dc, left), y0 = ty(dc, top);
    int x1 = tx(dc, right), y1 = ty(dc, bottom);
    int cx = (x0 + x1) / 2;
    int cy = (y0 + y1) / 2;
    int rx = (x1 - x0) / 2;
    int ry = (y1 - y0) / 2;
    int r = rx < ry ? rx : ry;

    gdi_object_t *brush = get_gdi_obj((HGDIOBJ)dc->current_brush);
    uint32_t color = brush ? colorref_to_gfx(brush->brush_color) : GFX_RGB(0, 0, 0);

    int cw, ch;
    uint32_t *canvas = dc_get_canvas(dc, &cw, &ch);
    if (canvas) {
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                if (dx * dx + dy * dy <= r * r) {
                    int px = cx + dx;
                    int py = cy + dy;
                    if (px >= 0 && px < cw && py >= 0 && py < ch)
                        canvas[py * cw + px] = color;
                }
            }
        }
    }

    if (!dc->is_memory_dc) wm_mark_dirty();
    return TRUE;
}

static BOOL WINAPI shim_BitBlt(
    HDC hdcDest, int xDest, int yDest, int w, int h,
    HDC hdcSrc, int xSrc, int ySrc, DWORD rop)
{
    (void)rop;

    int di = (int)hdcDest - 1;
    int si = (int)hdcSrc - 1;
    if (di < 0 || di >= MAX_DCS || si < 0 || si >= MAX_DCS) return FALSE;

    dc_state_t *dst_dc = &dc_table[di];
    dc_state_t *src_dc = &dc_table[si];

    int sw, sh2, dw, dh;
    uint32_t *src_canvas = dc_get_canvas(src_dc, &sw, &sh2);
    uint32_t *dst_canvas = dc_get_canvas(dst_dc, &dw, &dh);

    if (!src_canvas || !dst_canvas) return FALSE;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int sx = xSrc + x, sy = ySrc + y;
            int dx = xDest + x, dy = yDest + y;
            if (sx >= 0 && sx < sw && sy >= 0 && sy < sh2 &&
                dx >= 0 && dx < dw && dy >= 0 && dy < dh) {
                dst_canvas[dy * dw + dx] = src_canvas[sy * sw + sx];
            }
        }
    }

    if (!dst_dc->is_memory_dc) wm_mark_dirty();
    return TRUE;
}

/* ── Stock Object Getter ─────────────────────────────────────── */

static HGDIOBJ WINAPI shim_GetStockObject(int i) {
    init_stock_objects();
    switch (i) {
        case 0:  return (HGDIOBJ)(STOCK_WHITE_BRUSH + 1);  /* WHITE_BRUSH */
        case 4:  return (HGDIOBJ)(STOCK_BLACK_BRUSH + 1);  /* BLACK_BRUSH */
        case 5:  return (HGDIOBJ)(STOCK_NULL_BRUSH + 1);   /* NULL_BRUSH */
        case 6:  return (HGDIOBJ)(STOCK_WHITE_PEN + 1);    /* WHITE_PEN */
        case 7:  return (HGDIOBJ)(STOCK_BLACK_PEN + 1);    /* BLACK_PEN */
        case 8:  return (HGDIOBJ)(STOCK_NULL_PEN + 1);     /* NULL_PEN */
        case 13: return (HGDIOBJ)(STOCK_SYSTEM_FONT + 1);  /* SYSTEM_FONT */
        default: return (HGDIOBJ)0;
    }
}

/* ── CreateCompatibleDC / DeleteDC ───────────────────────────── */

static HDC WINAPI shim_CreateCompatibleDC(HDC hdc) {
    (void)hdc;
    init_stock_objects();

    for (int i = 0; i < MAX_DCS; i++) {
        if (!dc_table[i].in_use) {
            memset(&dc_table[i], 0, sizeof(dc_state_t));
            dc_table[i].in_use = 1;
            dc_table[i].wm_id = -1;
            dc_table[i].hwnd = 0;
            dc_init_defaults(&dc_table[i]);
            dc_table[i].is_memory_dc = 1;
            /* Start with a 1x1 buffer */
            dc_table[i].mem_buf = (uint32_t *)calloc(1, sizeof(uint32_t));
            dc_table[i].mem_w = 1;
            dc_table[i].mem_h = 1;
            return (HDC)(i + 1);
        }
    }
    return (HDC)0;
}

static BOOL WINAPI shim_DeleteDC(HDC hdc) {
    int idx = (int)hdc - 1;
    if (idx < 0 || idx >= MAX_DCS) return FALSE;

    dc_state_t *dc = &dc_table[idx];
    if (dc->is_memory_dc && dc->mem_buf) {
        /* Only free the 1x1 default buffer; bitmap-owned buffers belong to the bitmap object */
        if (dc->mem_w == 1 && dc->mem_h == 1) {
            free(dc->mem_buf);
        }
        dc->mem_buf = NULL;
    }
    dc->in_use = 0;
    return TRUE;
}

/* ── CreateCompatibleBitmap ──────────────────────────────────── */

static HBITMAP WINAPI shim_CreateCompatibleBitmap(HDC hdc, int w, int h) {
    (void)hdc;
    if (w <= 0 || h <= 0) return (HBITMAP)0;

    HGDIOBJ handle = alloc_gdi_obj(GDI_OBJ_BITMAP);
    if (!handle) return (HBITMAP)0;

    gdi_object_t *obj = get_gdi_obj(handle);
    obj->bitmap.bits = (uint32_t *)calloc(w * h, sizeof(uint32_t));
    if (!obj->bitmap.bits) {
        obj->type = GDI_OBJ_FREE;
        return (HBITMAP)0;
    }
    obj->bitmap.width = w;
    obj->bitmap.height = h;
    obj->bitmap.bpp = 32;

    return (HBITMAP)handle;
}

/* ── CreateDIBSection ────────────────────────────────────────── */

static HBITMAP WINAPI shim_CreateDIBSection(
    HDC hdc, const BITMAPINFO *pbmi, UINT usage,
    void **ppvBits, HANDLE hSection, DWORD offset)
{
    (void)hdc; (void)usage; (void)hSection; (void)offset;

    if (!pbmi) return (HBITMAP)0;
    int w = pbmi->bmiHeader.biWidth;
    int h = pbmi->bmiHeader.biHeight;
    if (h < 0) h = -h;  /* top-down bitmap */
    if (w <= 0 || h <= 0) return (HBITMAP)0;

    HGDIOBJ handle = alloc_gdi_obj(GDI_OBJ_BITMAP);
    if (!handle) return (HBITMAP)0;

    gdi_object_t *obj = get_gdi_obj(handle);
    obj->bitmap.bits = (uint32_t *)calloc(w * h, sizeof(uint32_t));
    if (!obj->bitmap.bits) {
        obj->type = GDI_OBJ_FREE;
        return (HBITMAP)0;
    }
    obj->bitmap.width = w;
    obj->bitmap.height = h;
    obj->bitmap.bpp = 32;

    if (ppvBits) *ppvBits = obj->bitmap.bits;

    return (HBITMAP)handle;
}

/* ── GetDIBits ───────────────────────────────────────────────── */

static int WINAPI shim_GetDIBits(
    HDC hdc, HBITMAP hbm, UINT start, UINT lines,
    LPVOID lpvBits, LPBITMAPINFO lpbmi, UINT usage)
{
    (void)hdc; (void)usage; (void)start;

    gdi_object_t *obj = get_gdi_obj((HGDIOBJ)hbm);
    if (!obj || obj->type != GDI_OBJ_BITMAP) return 0;

    if (lpbmi) {
        lpbmi->bmiHeader.biWidth = obj->bitmap.width;
        lpbmi->bmiHeader.biHeight = obj->bitmap.height;
        lpbmi->bmiHeader.biPlanes = 1;
        lpbmi->bmiHeader.biBitCount = 32;
        lpbmi->bmiHeader.biCompression = BI_RGB;
        lpbmi->bmiHeader.biSizeImage = obj->bitmap.width * obj->bitmap.height * 4;
    }

    if (lpvBits && obj->bitmap.bits) {
        UINT copy_lines = lines;
        if (copy_lines > (UINT)obj->bitmap.height) copy_lines = obj->bitmap.height;
        memcpy(lpvBits, obj->bitmap.bits, copy_lines * obj->bitmap.width * 4);
    }

    return (int)lines;
}

/* ── GetObjectA ──────────────────────────────────────────────── */

static int WINAPI shim_GetObjectA(HGDIOBJ h, int cb, LPVOID pv) {
    gdi_object_t *obj = get_gdi_obj(h);
    if (!obj) return 0;

    if (obj->type == GDI_OBJ_BITMAP) {
        if (!pv) return (int)sizeof(BITMAP);
        if (cb < (int)sizeof(BITMAP)) return 0;
        BITMAP *bm = (BITMAP *)pv;
        bm->bmType = 0;
        bm->bmWidth = obj->bitmap.width;
        bm->bmHeight = obj->bitmap.height;
        bm->bmWidthBytes = obj->bitmap.width * 4;
        bm->bmPlanes = 1;
        bm->bmBitsPixel = 32;
        bm->bmBits = obj->bitmap.bits;
        return (int)sizeof(BITMAP);
    }

    if (obj->type == GDI_OBJ_PEN) {
        /* LOGPEN-like: style, width, color */
        if (!pv) return 16;
        if (cb < 16) return 0;
        DWORD *out = (DWORD *)pv;
        out[0] = obj->pen.style;
        out[1] = obj->pen.width;
        out[2] = 0; /* POINT.y */
        out[3] = obj->pen.color;
        return 16;
    }

    if (obj->type == GDI_OBJ_FONT) {
        if (!pv) return (int)sizeof(LOGFONTA);
        if (cb < (int)sizeof(LOGFONTA)) return 0;
        LOGFONTA *lf = (LOGFONTA *)pv;
        memset(lf, 0, sizeof(LOGFONTA));
        lf->lfHeight = obj->font.height;
        lf->lfWeight = obj->font.weight;
        strncpy(lf->lfFaceName, obj->font.face_name, 31);
        return (int)sizeof(LOGFONTA);
    }

    return 0;
}

/* ── CreatePen ───────────────────────────────────────────────── */

static HPEN WINAPI shim_CreatePen(int iStyle, int cWidth, COLORREF color) {
    HGDIOBJ h = alloc_gdi_obj(GDI_OBJ_PEN);
    if (!h) return (HPEN)0;

    gdi_object_t *obj = get_gdi_obj(h);
    obj->pen.style = iStyle;
    obj->pen.width = cWidth;
    obj->pen.color = color;

    return (HPEN)h;
}

/* ── MoveToEx / LineTo ───────────────────────────────────────── */

static BOOL WINAPI shim_MoveToEx(HDC hdc, int x, int y, LPPOINT lppt) {
    int idx = (int)hdc - 1;
    if (idx < 0 || idx >= MAX_DCS || !dc_table[idx].in_use) return FALSE;

    if (lppt) {
        lppt->x = dc_table[idx].cur_x;
        lppt->y = dc_table[idx].cur_y;
    }
    dc_table[idx].cur_x = x;
    dc_table[idx].cur_y = y;
    return TRUE;
}

/* Bresenham line drawing to a raw pixel buffer */
static void draw_line_buf(uint32_t *buf, int bw, int bh,
                          int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = x1 - x0, dy = y1 - y0;
    int sx = dx > 0 ? 1 : -1;
    int sy = dy > 0 ? 1 : -1;
    dx = dx < 0 ? -dx : dx;
    dy = dy < 0 ? -dy : dy;
    int err = dx - dy;

    while (1) {
        if (x0 >= 0 && x0 < bw && y0 >= 0 && y0 < bh)
            buf[y0 * bw + x0] = color;
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

static BOOL WINAPI shim_LineTo(HDC hdc, int x, int y) {
    int idx = (int)hdc - 1;
    if (idx < 0 || idx >= MAX_DCS || !dc_table[idx].in_use) return FALSE;

    dc_state_t *dc = &dc_table[idx];
    int x0 = tx(dc, dc->cur_x), y0 = ty(dc, dc->cur_y);
    int x1 = tx(dc, x), y1 = ty(dc, y);

    gdi_object_t *pen = get_gdi_obj((HGDIOBJ)dc->current_pen);
    uint32_t color = pen && pen->type == GDI_OBJ_PEN ? colorref_to_gfx(pen->pen.color) : GFX_RGB(0, 0, 0);

    if (dc->is_memory_dc) {
        if (dc->mem_buf)
            draw_line_buf(dc->mem_buf, dc->mem_w, dc->mem_h, x0, y0, x1, y1, color);
    } else {
        wm_draw_line(dc->wm_id, x0, y0, x1, y1, color);
        wm_mark_dirty();
    }

    dc->cur_x = x;
    dc->cur_y = y;
    return TRUE;
}

/* ── Polyline / Polygon ──────────────────────────────────────── */

static BOOL WINAPI shim_Polyline(HDC hdc, const POINT *apt, int cpt) {
    int idx = (int)hdc - 1;
    if (idx < 0 || idx >= MAX_DCS || !dc_table[idx].in_use || cpt < 2) return FALSE;

    dc_state_t *dc = &dc_table[idx];
    gdi_object_t *pen = get_gdi_obj((HGDIOBJ)dc->current_pen);
    uint32_t color = pen && pen->type == GDI_OBJ_PEN ? colorref_to_gfx(pen->pen.color) : GFX_RGB(0, 0, 0);

    int cw, ch;
    uint32_t *canvas = dc_get_canvas(dc, &cw, &ch);
    if (!canvas) return FALSE;

    for (int i = 0; i < cpt - 1; i++) {
        int x0 = tx(dc, apt[i].x), y0 = ty(dc, apt[i].y);
        int x1 = tx(dc, apt[i + 1].x), y1 = ty(dc, apt[i + 1].y);
        draw_line_buf(canvas, cw, ch, x0, y0, x1, y1, color);
    }

    if (!dc->is_memory_dc) wm_mark_dirty();
    return TRUE;
}

static BOOL WINAPI shim_Polygon(HDC hdc, const POINT *apt, int cpt) {
    if (cpt < 2) return FALSE;

    /* Draw outline like polyline, then close the shape */
    shim_Polyline(hdc, apt, cpt);

    /* Close: draw line from last to first */
    int idx = (int)hdc - 1;
    if (idx < 0 || idx >= MAX_DCS) return FALSE;
    dc_state_t *dc = &dc_table[idx];

    gdi_object_t *pen = get_gdi_obj((HGDIOBJ)dc->current_pen);
    uint32_t color = pen && pen->type == GDI_OBJ_PEN ? colorref_to_gfx(pen->pen.color) : GFX_RGB(0, 0, 0);

    int cw, ch;
    uint32_t *canvas = dc_get_canvas(dc, &cw, &ch);
    if (canvas) {
        int x0 = tx(dc, apt[cpt - 1].x), y0 = ty(dc, apt[cpt - 1].y);
        int x1 = tx(dc, apt[0].x), y1 = ty(dc, apt[0].y);
        draw_line_buf(canvas, cw, ch, x0, y0, x1, y1, color);
    }

    if (!dc->is_memory_dc) wm_mark_dirty();
    return TRUE;
}

/* ── RoundRect ───────────────────────────────────────────────── */

static BOOL WINAPI shim_RoundRect(HDC hdc, int left, int top, int right, int bottom, int rw, int rh) {
    int idx = (int)hdc - 1;
    if (idx < 0 || idx >= MAX_DCS || !dc_table[idx].in_use) return FALSE;

    dc_state_t *dc = &dc_table[idx];
    int x = tx(dc, left), y = ty(dc, top);
    int w = right - left, h = bottom - top;
    int r = (rw < rh ? rw : rh) / 2;

    gdi_object_t *brush = get_gdi_obj((HGDIOBJ)dc->current_brush);
    uint32_t fill = brush ? colorref_to_gfx(brush->brush_color) : GFX_RGB(255, 255, 255);

    if (!dc->is_memory_dc) {
        wm_fill_rounded_rect(dc->wm_id, x, y, w, h, r, fill);
        wm_mark_dirty();
    } else {
        /* Fallback: plain rect in memory DC */
        if (dc->mem_buf) {
            for (int row = 0; row < h; row++)
                for (int col = 0; col < w; col++) {
                    int px = x + col, py = y + row;
                    if (px >= 0 && px < dc->mem_w && py >= 0 && py < dc->mem_h)
                        dc->mem_buf[py * dc->mem_w + px] = fill;
                }
        }
    }
    return TRUE;
}

/* ── Arc (stub) ──────────────────────────────────────────────── */

static BOOL WINAPI shim_Arc(HDC hdc, int x1, int y1, int x2, int y2,
                             int x3, int y3, int x4, int y4) {
    (void)hdc; (void)x1; (void)y1; (void)x2; (void)y2;
    (void)x3; (void)y3; (void)x4; (void)y4;
    return TRUE;
}

/* ── Text Measurement ────────────────────────────────────────── */

static BOOL WINAPI shim_GetTextMetricsA(HDC hdc, LPTEXTMETRICA lptm) {
    (void)hdc;
    if (!lptm) return FALSE;

    memset(lptm, 0, sizeof(TEXTMETRICA));
    lptm->tmHeight = 16;
    lptm->tmAscent = 12;
    lptm->tmDescent = 4;
    lptm->tmInternalLeading = 0;
    lptm->tmExternalLeading = 0;
    lptm->tmAveCharWidth = 8;
    lptm->tmMaxCharWidth = 8;
    lptm->tmWeight = 400;
    lptm->tmOverhang = 0;
    lptm->tmDigitizedAspectX = 96;
    lptm->tmDigitizedAspectY = 96;
    lptm->tmFirstChar = 0x20;
    lptm->tmLastChar = 0x7E;
    lptm->tmDefaultChar = '?';
    lptm->tmBreakChar = ' ';
    lptm->tmPitchAndFamily = 0x31; /* TMPF_FIXED_PITCH | FF_MODERN */
    lptm->tmCharSet = 0; /* ANSI_CHARSET */

    return TRUE;
}

static BOOL WINAPI shim_GetTextExtentPoint32A(HDC hdc, LPCSTR lpString, int c, LPSIZE lpSize) {
    (void)hdc; (void)lpString;
    if (!lpSize) return FALSE;

    lpSize->cx = c * 8;
    lpSize->cy = 16;
    return TRUE;
}

static HFONT WINAPI shim_CreateFontIndirectA(const LOGFONTA *lplf) {
    if (!lplf) return (HFONT)0;
    return shim_CreateFontA(
        lplf->lfHeight, lplf->lfWidth, lplf->lfEscapement, lplf->lfOrientation,
        lplf->lfWeight, lplf->lfItalic, lplf->lfUnderline, lplf->lfStrikeOut,
        lplf->lfCharSet, lplf->lfOutPrecision, lplf->lfClipPrecision,
        lplf->lfQuality, lplf->lfPitchAndFamily, lplf->lfFaceName);
}

static int WINAPI shim_EnumFontFamiliesExA(
    HDC hdc, LPLOGFONTA lpLogfont, FONTENUMPROCA lpProc, LPARAM lParam, DWORD dwFlags)
{
    (void)hdc; (void)lpLogfont; (void)dwFlags;

    if (!lpProc) return 0;

    /* Report a single "System" font */
    ENUMLOGFONTEXA elf;
    memset(&elf, 0, sizeof(elf));
    elf.elfLogFont.lfHeight = 16;
    elf.elfLogFont.lfWeight = 400;
    strcpy(elf.elfLogFont.lfFaceName, "System");
    strcpy(elf.elfFullName, "System");
    strcpy(elf.elfStyle, "Regular");
    strcpy(elf.elfScript, "Western");

    NEWTEXTMETRICEXA ntm;
    memset(&ntm, 0, sizeof(ntm));
    ntm.ntmTm.tmHeight = 16;
    ntm.ntmTm.tmAscent = 12;
    ntm.ntmTm.tmDescent = 4;
    ntm.ntmTm.tmAveCharWidth = 8;
    ntm.ntmTm.tmMaxCharWidth = 8;
    ntm.ntmTm.tmWeight = 400;

    return lpProc(&elf, &ntm, 4 /* TRUETYPE_FONTTYPE */, lParam);
}

static int WINAPI shim_GetTextFaceA(HDC hdc, int c, LPSTR lpName) {
    (void)hdc;
    const char *name = "System";
    if (lpName && c > 0) {
        strncpy(lpName, name, c - 1);
        lpName[c - 1] = '\0';
    }
    return (int)strlen(name);
}

/* ── SaveDC / RestoreDC ──────────────────────────────────────── */

static int WINAPI shim_SaveDC(HDC hdc) {
    int idx = (int)hdc - 1;
    if (idx < 0 || idx >= MAX_DCS || !dc_table[idx].in_use) return 0;

    dc_state_t *dc = &dc_table[idx];
    if (dc->save_level >= DC_SAVE_STACK_DEPTH) return 0;

    dc_saved_state_t *s = &dc->save_stack[dc->save_level];
    s->text_color = dc->text_color;
    s->bg_color = dc->bg_color;
    s->bg_mode = dc->bg_mode;
    s->current_brush = dc->current_brush;
    s->current_font = dc->current_font;
    s->current_pen = dc->current_pen;
    s->cur_x = dc->cur_x;
    s->cur_y = dc->cur_y;
    s->viewport_org_x = dc->viewport_org_x;
    s->viewport_org_y = dc->viewport_org_y;
    s->window_org_x = dc->window_org_x;
    s->window_org_y = dc->window_org_y;
    s->clip_left = dc->clip_left;
    s->clip_top = dc->clip_top;
    s->clip_right = dc->clip_right;
    s->clip_bottom = dc->clip_bottom;
    s->has_clip = dc->has_clip;

    dc->save_level++;
    return dc->save_level;
}

static BOOL WINAPI shim_RestoreDC(HDC hdc, int nSavedDC) {
    int idx = (int)hdc - 1;
    if (idx < 0 || idx >= MAX_DCS || !dc_table[idx].in_use) return FALSE;

    dc_state_t *dc = &dc_table[idx];

    int target;
    if (nSavedDC < 0) {
        target = dc->save_level + nSavedDC;
    } else {
        target = nSavedDC - 1;
    }

    if (target < 0 || target >= dc->save_level) return FALSE;

    dc_saved_state_t *s = &dc->save_stack[target];
    dc->text_color = s->text_color;
    dc->bg_color = s->bg_color;
    dc->bg_mode = s->bg_mode;
    dc->current_brush = s->current_brush;
    dc->current_font = s->current_font;
    dc->current_pen = s->current_pen;
    dc->cur_x = s->cur_x;
    dc->cur_y = s->cur_y;
    dc->viewport_org_x = s->viewport_org_x;
    dc->viewport_org_y = s->viewport_org_y;
    dc->window_org_x = s->window_org_x;
    dc->window_org_y = s->window_org_y;
    dc->clip_left = s->clip_left;
    dc->clip_top = s->clip_top;
    dc->clip_right = s->clip_right;
    dc->clip_bottom = s->clip_bottom;
    dc->has_clip = s->has_clip;

    dc->save_level = target;
    return TRUE;
}

/* ── SetViewportOrgEx / SetWindowOrgEx ───────────────────────── */

static BOOL WINAPI shim_SetViewportOrgEx(HDC hdc, int x, int y, LPPOINT lppt) {
    int idx = (int)hdc - 1;
    if (idx < 0 || idx >= MAX_DCS) return FALSE;

    if (lppt) {
        lppt->x = dc_table[idx].viewport_org_x;
        lppt->y = dc_table[idx].viewport_org_y;
    }
    dc_table[idx].viewport_org_x = x;
    dc_table[idx].viewport_org_y = y;
    return TRUE;
}

static BOOL WINAPI shim_SetWindowOrgEx(HDC hdc, int x, int y, LPPOINT lppt) {
    int idx = (int)hdc - 1;
    if (idx < 0 || idx >= MAX_DCS) return FALSE;

    if (lppt) {
        lppt->x = dc_table[idx].window_org_x;
        lppt->y = dc_table[idx].window_org_y;
    }
    dc_table[idx].window_org_x = x;
    dc_table[idx].window_org_y = y;
    return TRUE;
}

/* ── GetDeviceCaps ───────────────────────────────────────────── */

static int WINAPI shim_GetDeviceCaps(HDC hdc, int index) {
    (void)hdc;
    switch (index) {
        case HORZRES:     return 1920;
        case VERTRES:     return 1080;
        case BITSPIXEL:   return 32;
        case PLANES:      return 1;
        case LOGPIXELSX:  return 96;
        case LOGPIXELSY:  return 96;
        case SIZEPALETTE: return 0;
        case NUMCOLORS:   return -1;
        case RASTERCAPS:  return 0x7E99;
        case TECHNOLOGY:  return DT_RASDISPLAY;
        default:          return 0;
    }
}

/* ── Clipping ────────────────────────────────────────────────── */

static int WINAPI shim_IntersectClipRect(HDC hdc, int left, int top, int right, int bottom) {
    int idx = (int)hdc - 1;
    if (idx < 0 || idx >= MAX_DCS) return 0; /* ERROR */

    dc_state_t *dc = &dc_table[idx];
    if (dc->has_clip) {
        if (left > dc->clip_left) dc->clip_left = left;
        if (top > dc->clip_top) dc->clip_top = top;
        if (right < dc->clip_right) dc->clip_right = right;
        if (bottom < dc->clip_bottom) dc->clip_bottom = bottom;
    } else {
        dc->clip_left = left;
        dc->clip_top = top;
        dc->clip_right = right;
        dc->clip_bottom = bottom;
        dc->has_clip = 1;
    }

    if (dc->clip_left >= dc->clip_right || dc->clip_top >= dc->clip_bottom)
        return 1; /* NULLREGION */
    return 2; /* SIMPLEREGION */
}

static int WINAPI shim_SelectClipRgn(HDC hdc, HRGN hrgn) {
    int idx = (int)hdc - 1;
    if (idx < 0 || idx >= MAX_DCS) return 0;

    dc_state_t *dc = &dc_table[idx];
    if (hrgn == 0) {
        dc->has_clip = 0;
        return 2; /* SIMPLEREGION */
    }

    gdi_object_t *obj = get_gdi_obj((HGDIOBJ)hrgn);
    if (!obj || obj->type != GDI_OBJ_REGION) return 0;

    dc->clip_left = obj->region.left;
    dc->clip_top = obj->region.top;
    dc->clip_right = obj->region.right;
    dc->clip_bottom = obj->region.bottom;
    dc->has_clip = 1;
    return 2; /* SIMPLEREGION */
}

static HRGN WINAPI shim_CreateRectRgn(int left, int top, int right, int bottom) {
    HGDIOBJ h = alloc_gdi_obj(GDI_OBJ_REGION);
    if (!h) return (HRGN)0;

    gdi_object_t *obj = get_gdi_obj(h);
    obj->region.left = left;
    obj->region.top = top;
    obj->region.right = right;
    obj->region.bottom = bottom;

    return (HRGN)h;
}

static int WINAPI shim_ExcludeClipRect(HDC hdc, int left, int top, int right, int bottom) {
    (void)hdc; (void)left; (void)top; (void)right; (void)bottom;
    return 2; /* SIMPLEREGION — stub */
}

/* ── StretchBlt ──────────────────────────────────────────────── */

static BOOL WINAPI shim_StretchBlt(
    HDC hdcDest, int xDest, int yDest, int wDest, int hDest,
    HDC hdcSrc, int xSrc, int ySrc, int wSrc, int hSrc, DWORD rop)
{
    (void)rop;

    int di = (int)hdcDest - 1;
    int si = (int)hdcSrc - 1;
    if (di < 0 || di >= MAX_DCS || si < 0 || si >= MAX_DCS) return FALSE;

    dc_state_t *dst_dc = &dc_table[di];
    dc_state_t *src_dc = &dc_table[si];

    int sw, sh2, dw, dh;
    uint32_t *src_canvas = dc_get_canvas(src_dc, &sw, &sh2);
    uint32_t *dst_canvas = dc_get_canvas(dst_dc, &dw, &dh);
    if (!src_canvas || !dst_canvas) return FALSE;

    /* Nearest-neighbor scaling */
    for (int y = 0; y < hDest; y++) {
        int sy = ySrc + (y * hSrc) / hDest;
        for (int x = 0; x < wDest; x++) {
            int sx = xSrc + (x * wSrc) / wDest;
            int dx_px = xDest + x, dy_px = yDest + y;
            if (sx >= 0 && sx < sw && sy >= 0 && sy < sh2 &&
                dx_px >= 0 && dx_px < dw && dy_px >= 0 && dy_px < dh) {
                dst_canvas[dy_px * dw + dx_px] = src_canvas[sy * sw + sx];
            }
        }
    }

    if (!dst_dc->is_memory_dc) wm_mark_dirty();
    return TRUE;
}

/* ── StretchDIBits ───────────────────────────────────────────── */

static int WINAPI shim_StretchDIBits(
    HDC hdc, int xDest, int yDest, int wDest, int hDest,
    int xSrc, int ySrc, int wSrc, int hSrc,
    const void *lpBits, const BITMAPINFO *lpbmi, UINT iUsage, DWORD rop)
{
    (void)iUsage; (void)rop;

    int idx = (int)hdc - 1;
    if (idx < 0 || idx >= MAX_DCS || !dc_table[idx].in_use) return 0;
    if (!lpBits || !lpbmi) return 0;

    dc_state_t *dc = &dc_table[idx];
    int dw, dh;
    uint32_t *dst_canvas = dc_get_canvas(dc, &dw, &dh);
    if (!dst_canvas) return 0;

    int bmp_w = lpbmi->bmiHeader.biWidth;
    int bmp_h = lpbmi->bmiHeader.biHeight;
    int top_down = (bmp_h < 0);
    if (bmp_h < 0) bmp_h = -bmp_h;

    const uint32_t *src = (const uint32_t *)lpBits;

    for (int y = 0; y < hDest; y++) {
        int sy = ySrc + (y * hSrc) / hDest;
        if (top_down) {
            /* top-down: row 0 is at top */
        } else {
            /* bottom-up: flip */
            sy = bmp_h - 1 - sy;
        }
        for (int x = 0; x < wDest; x++) {
            int sx = xSrc + (x * wSrc) / wDest;
            int dx_px = xDest + x, dy_px = yDest + y;
            if (sx >= 0 && sx < bmp_w && sy >= 0 && sy < bmp_h &&
                dx_px >= 0 && dx_px < dw && dy_px >= 0 && dy_px < dh) {
                dst_canvas[dy_px * dw + dx_px] = src[sy * bmp_w + sx];
            }
        }
    }

    if (!dc->is_memory_dc) wm_mark_dirty();
    return hDest;
}

/* ── SetDIBitsToDevice ───────────────────────────────────────── */

static int WINAPI shim_SetDIBitsToDevice(
    HDC hdc, int xDest, int yDest, DWORD w, DWORD h,
    int xSrc, int ySrc, UINT startScan, UINT numScans,
    const void *lpBits, const BITMAPINFO *lpbmi, UINT colorUse)
{
    (void)startScan; (void)numScans; (void)colorUse;

    int idx = (int)hdc - 1;
    if (idx < 0 || idx >= MAX_DCS || !dc_table[idx].in_use) return 0;
    if (!lpBits || !lpbmi) return 0;

    dc_state_t *dc = &dc_table[idx];
    int dw, dh2;
    uint32_t *dst_canvas = dc_get_canvas(dc, &dw, &dh2);
    if (!dst_canvas) return 0;

    int bmp_w = lpbmi->bmiHeader.biWidth;
    int bmp_h = lpbmi->bmiHeader.biHeight;
    int top_down = (bmp_h < 0);
    if (bmp_h < 0) bmp_h = -bmp_h;

    const uint32_t *src = (const uint32_t *)lpBits;

    for (int y = 0; y < (int)h; y++) {
        int sy = ySrc + y;
        int src_row = top_down ? sy : (bmp_h - 1 - sy);
        for (int x = 0; x < (int)w; x++) {
            int sx = xSrc + x;
            int dx_px = xDest + x, dy_px = yDest + y;
            if (sx >= 0 && sx < bmp_w && src_row >= 0 && src_row < bmp_h &&
                dx_px >= 0 && dx_px < dw && dy_px >= 0 && dy_px < dh2) {
                dst_canvas[dy_px * dw + dx_px] = src[src_row * bmp_w + sx];
            }
        }
    }

    if (!dc->is_memory_dc) wm_mark_dirty();
    return (int)h;
}

/* ── Export Table ─────────────────────────────────────────────── */

/* ── W-suffix Wrappers ───────────────────────────────────────── */

/* UTF-8 ↔ UTF-16 helpers from kernel32 */
extern int win32_utf8_to_wchar(const char *utf8, int utf8_len, WCHAR *out, int out_len);
extern int win32_wchar_to_utf8(const WCHAR *wstr, int wstr_len, char *out, int out_len);

static BOOL WINAPI shim_TextOutW(HDC hdc, int x, int y, LPCWSTR lpString, int c) {
    char narrow[512];
    int len = win32_wchar_to_utf8(lpString, c, narrow, sizeof(narrow) - 1);
    if (len > 0 && narrow[len - 1] == '\0') len--;
    narrow[len < (int)sizeof(narrow) ? len : (int)sizeof(narrow) - 1] = '\0';
    return shim_TextOutA(hdc, x, y, narrow, len);
}

static HFONT WINAPI shim_CreateFontW(
    int cHeight, int cWidth, int cEscapement, int cOrientation,
    int cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut,
    DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision,
    DWORD iQuality, DWORD iPitchAndFamily, LPCWSTR pszFaceName)
{
    char narrow[64] = {0};
    if (pszFaceName)
        win32_wchar_to_utf8(pszFaceName, -1, narrow, sizeof(narrow));
    return shim_CreateFontA(cHeight, cWidth, cEscapement, cOrientation,
        cWeight, bItalic, bUnderline, bStrikeOut,
        iCharSet, iOutPrecision, iClipPrecision,
        iQuality, iPitchAndFamily, pszFaceName ? narrow : NULL);
}

static HFONT WINAPI shim_CreateFontIndirectW(const LOGFONTW *lplf) {
    if (!lplf) return (HFONT)0;
    LOGFONTA a;
    memcpy(&a, lplf, sizeof(a)); /* same layout up to lfFaceName */
    win32_wchar_to_utf8(lplf->lfFaceName, 32, a.lfFaceName, sizeof(a.lfFaceName));
    a.lfFaceName[31] = '\0';
    return shim_CreateFontIndirectA(&a);
}

static BOOL WINAPI shim_GetTextExtentPoint32W(HDC hdc, LPCWSTR lpString, int c, LPSIZE lpSize) {
    /* Our font is fixed-width 8x16, so width = chars * 8 regardless of encoding */
    (void)hdc; (void)lpString;
    if (!lpSize) return FALSE;
    lpSize->cx = c * 8;
    lpSize->cy = 16;
    return TRUE;
}

static int WINAPI shim_EnumFontFamiliesExW(
    HDC hdc, LPLOGFONTW lpLogfont, FONTENUMPROCA lpProc, LPARAM lParam, DWORD dwFlags)
{
    /* Delegate to A version — callback uses ENUMLOGFONTEXA which is A-only */
    LOGFONTA a;
    memset(&a, 0, sizeof(a));
    if (lpLogfont)
        win32_wchar_to_utf8(lpLogfont->lfFaceName, 32, a.lfFaceName, sizeof(a.lfFaceName));
    return shim_EnumFontFamiliesExA(hdc, &a, lpProc, lParam, dwFlags);
}

static int WINAPI shim_GetTextFaceW(HDC hdc, int c, LPWSTR lpName) {
    char narrow[64];
    int ret = shim_GetTextFaceA(hdc, sizeof(narrow), narrow);
    if (lpName && c > 0)
        win32_utf8_to_wchar(narrow, -1, lpName, c);
    return ret;
}

static int WINAPI shim_GetObjectW(HGDIOBJ h, int cb, LPVOID pv) {
    /* For LOGFONTW output, delegate to A and convert */
    if (cb >= (int)sizeof(LOGFONTW) && pv) {
        /* Try as font first */
        LOGFONTA a;
        int ret = shim_GetObjectA(h, sizeof(LOGFONTA), &a);
        if (ret == (int)sizeof(LOGFONTA)) {
            LOGFONTW *w = (LOGFONTW *)pv;
            memcpy(w, &a, (size_t)((char *)a.lfFaceName - (char *)&a));
            win32_utf8_to_wchar(a.lfFaceName, -1, w->lfFaceName, 32);
            return (int)sizeof(LOGFONTW);
        }
    }
    /* Fall through to A version for non-font objects */
    return shim_GetObjectA(h, cb, pv);
}

static const win32_export_entry_t gdi32_exports[] = {
    /* Original functions */
    { "GetDC",                  (void *)shim_GetDC },
    { "ReleaseDC",              (void *)shim_ReleaseDC },
    { "BeginPaint",             (void *)shim_BeginPaint },
    { "EndPaint",               (void *)shim_EndPaint },
    { "TextOutA",               (void *)shim_TextOutA },
    { "SetPixel",               (void *)shim_SetPixel },
    { "GetPixel",               (void *)shim_GetPixel },
    { "CreateSolidBrush",       (void *)shim_CreateSolidBrush },
    { "FillRect",               (void *)shim_FillRect },
    { "SelectObject",           (void *)shim_SelectObject },
    { "DeleteObject",           (void *)shim_DeleteObject },
    { "CreateFontA",            (void *)shim_CreateFontA },
    { "SetTextColor",           (void *)shim_SetTextColor },
    { "SetBkColor",             (void *)shim_SetBkColor },
    { "SetBkMode",              (void *)shim_SetBkMode },
    { "Rectangle",              (void *)shim_Rectangle },
    { "Ellipse",                (void *)shim_Ellipse },
    { "BitBlt",                 (void *)shim_BitBlt },
    { "GetStockObject",         (void *)shim_GetStockObject },
    /* Phase 8: Offscreen rendering */
    { "CreateCompatibleDC",     (void *)shim_CreateCompatibleDC },
    { "DeleteDC",               (void *)shim_DeleteDC },
    { "CreateCompatibleBitmap", (void *)shim_CreateCompatibleBitmap },
    { "CreateDIBSection",       (void *)shim_CreateDIBSection },
    { "GetDIBits",              (void *)shim_GetDIBits },
    { "GetObjectA",             (void *)shim_GetObjectA },
    /* Phase 8: Image blitting */
    { "StretchBlt",             (void *)shim_StretchBlt },
    { "StretchDIBits",          (void *)shim_StretchDIBits },
    { "SetDIBitsToDevice",      (void *)shim_SetDIBitsToDevice },
    /* Phase 8: Pen & line drawing */
    { "CreatePen",              (void *)shim_CreatePen },
    { "MoveToEx",               (void *)shim_MoveToEx },
    { "LineTo",                 (void *)shim_LineTo },
    { "Polyline",               (void *)shim_Polyline },
    { "Polygon",                (void *)shim_Polygon },
    { "RoundRect",              (void *)shim_RoundRect },
    { "Arc",                    (void *)shim_Arc },
    /* Phase 8: Text measurement */
    { "GetTextMetricsA",        (void *)shim_GetTextMetricsA },
    { "GetTextExtentPoint32A",  (void *)shim_GetTextExtentPoint32A },
    { "CreateFontIndirectA",    (void *)shim_CreateFontIndirectA },
    { "EnumFontFamiliesExA",    (void *)shim_EnumFontFamiliesExA },
    { "GetTextFaceA",           (void *)shim_GetTextFaceA },
    /* Phase 8: DC state */
    { "SaveDC",                 (void *)shim_SaveDC },
    { "RestoreDC",              (void *)shim_RestoreDC },
    { "SetViewportOrgEx",       (void *)shim_SetViewportOrgEx },
    { "SetWindowOrgEx",         (void *)shim_SetWindowOrgEx },
    { "GetDeviceCaps",          (void *)shim_GetDeviceCaps },
    /* W-suffix variants */
    { "TextOutW",               (void *)shim_TextOutW },
    { "CreateFontW",            (void *)shim_CreateFontW },
    { "CreateFontIndirectW",    (void *)shim_CreateFontIndirectW },
    { "GetTextExtentPoint32W",  (void *)shim_GetTextExtentPoint32W },
    { "EnumFontFamiliesExW",    (void *)shim_EnumFontFamiliesExW },
    { "GetTextFaceW",           (void *)shim_GetTextFaceW },
    { "GetObjectW",             (void *)shim_GetObjectW },
    /* Phase 8: Clipping */
    { "IntersectClipRect",      (void *)shim_IntersectClipRect },
    { "SelectClipRgn",          (void *)shim_SelectClipRgn },
    { "CreateRectRgn",          (void *)shim_CreateRectRgn },
    { "ExcludeClipRect",        (void *)shim_ExcludeClipRect },
};

const win32_dll_shim_t win32_gdi32 = {
    .dll_name = "gdi32.dll",
    .exports = gdi32_exports,
    .num_exports = sizeof(gdi32_exports) / sizeof(gdi32_exports[0]),
};
