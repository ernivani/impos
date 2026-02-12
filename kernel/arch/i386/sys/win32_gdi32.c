#include <kernel/win32_types.h>
#include <kernel/wm.h>
#include <kernel/gfx.h>
#include <kernel/task.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── DC (Device Context) Management ──────────────────────────── */
#define MAX_DCS 32

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
} dc_state_t;

static dc_state_t dc_table[MAX_DCS];

/* ── GDI Object (Brush/Font/Pen) Table ───────────────────────── */
#define MAX_GDI_OBJECTS 64

typedef enum {
    GDI_OBJ_FREE = 0,
    GDI_OBJ_BRUSH,
    GDI_OBJ_FONT,
    GDI_OBJ_PEN,
    GDI_OBJ_BITMAP,
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
        COLORREF pen_color;
    };
} gdi_object_t;

static gdi_object_t gdi_objects[MAX_GDI_OBJECTS];

/* Stock objects (pre-allocated) */
#define STOCK_WHITE_BRUSH    1
#define STOCK_BLACK_BRUSH    2
#define STOCK_NULL_BRUSH     3
#define STOCK_SYSTEM_FONT    4

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
}

static HGDIOBJ alloc_gdi_obj(gdi_obj_type_t type) {
    init_stock_objects();
    for (int i = 10; i < MAX_GDI_OBJECTS; i++) {  /* skip stock objects */
        if (gdi_objects[i].type == GDI_OBJ_FREE) {
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

/* ── Resolve HWND to WM id (from user32's window table) ────── */
/* We need to access user32's window info. Since this is all in-kernel,
 * we'll just iterate WM windows to find one matching the task. */

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

/* ── GetDC / ReleaseDC ───────────────────────────────────────── */

static HDC WINAPI shim_GetDC(HWND hWnd) {
    init_stock_objects();

    for (int i = 0; i < MAX_DCS; i++) {
        if (!dc_table[i].in_use) {
            dc_table[i].in_use = 1;
            dc_table[i].hwnd = hWnd;
            dc_table[i].wm_id = hwnd_to_wm_id(hWnd);
            dc_table[i].text_color = RGB(0, 0, 0);
            dc_table[i].bg_color = RGB(255, 255, 255);
            dc_table[i].bg_mode = OPAQUE;
            dc_table[i].is_paint_dc = 0;
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
    uint32_t fg = colorref_to_gfx(dc->text_color);
    uint32_t bg = (dc->bg_mode == TRANSPARENT) ? 0 : colorref_to_gfx(dc->bg_color);

    /* Draw character by character using WM canvas API */
    for (int i = 0; i < c && lpString[i]; i++) {
        if (dc->bg_mode == OPAQUE) {
            wm_fill_rect(dc->wm_id, x + i * 8, y, 8, 16, bg);
        }
        wm_draw_char(dc->wm_id, x + i * 8, y, lpString[i], fg,
                      dc->bg_mode == TRANSPARENT ? 0x00000000 : bg);
    }

    wm_mark_dirty();
    return TRUE;
}

static COLORREF WINAPI shim_SetPixel(HDC hdc, int x, int y, COLORREF color) {
    int idx = (int)hdc - 1;
    if (idx < 0 || idx >= MAX_DCS || !dc_table[idx].in_use) return 0;

    wm_put_pixel(dc_table[idx].wm_id, x, y, colorref_to_gfx(color));
    return color;
}

static COLORREF WINAPI shim_GetPixel(HDC hdc, int x, int y) {
    int idx = (int)hdc - 1;
    if (idx < 0 || idx >= MAX_DCS || !dc_table[idx].in_use) return 0;

    int cw, ch;
    uint32_t *canvas = wm_get_canvas(dc_table[idx].wm_id, &cw, &ch);
    if (!canvas || x < 0 || y < 0 || x >= cw || y >= ch) return 0;

    uint32_t pixel = canvas[y * cw + x];
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

    gdi_object_t *brush = get_gdi_obj((HGDIOBJ)hbr);
    uint32_t color;

    if (brush && brush->type == GDI_OBJ_BRUSH) {
        color = colorref_to_gfx(brush->brush_color);
    } else {
        /* System color brush: (COLOR_WINDOW + 1), etc. */
        color = GFX_RGB(240, 240, 240);
    }

    wm_fill_rect(dc_table[idx].wm_id,
                  lprc->left, lprc->top,
                  lprc->right - lprc->left,
                  lprc->bottom - lprc->top,
                  color);
    wm_mark_dirty();
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
        default:
            break;
    }
    return old;
}

static BOOL WINAPI shim_DeleteObject(HGDIOBJ h) {
    int idx = (int)h - 1;
    if (idx < 0 || idx >= MAX_GDI_OBJECTS) return FALSE;
    if (idx < 10) return FALSE;  /* Can't delete stock objects */
    gdi_objects[idx].type = GDI_OBJ_FREE;
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

    /* Fill with current brush */
    gdi_object_t *brush = get_gdi_obj((HGDIOBJ)dc->current_brush);
    if (brush && brush->type == GDI_OBJ_BRUSH) {
        wm_fill_rect(dc->wm_id, left, top,
                      right - left, bottom - top,
                      colorref_to_gfx(brush->brush_color));
    }

    /* Outline with current pen */
    gdi_object_t *pen = get_gdi_obj((HGDIOBJ)dc->current_pen);
    uint32_t outline = pen ? colorref_to_gfx(pen->pen_color) : GFX_RGB(0, 0, 0);
    wm_draw_rect(dc->wm_id, left, top, right - left, bottom - top, outline);

    wm_mark_dirty();
    return TRUE;
}

static BOOL WINAPI shim_Ellipse(HDC hdc, int left, int top, int right, int bottom) {
    int idx = (int)hdc - 1;
    if (idx < 0 || idx >= MAX_DCS || !dc_table[idx].in_use) return FALSE;

    /* Approximate with filled rect for now — full ellipse would need gfx_fill_circle */
    dc_state_t *dc = &dc_table[idx];
    int cx = (left + right) / 2;
    int cy = (top + bottom) / 2;
    int rx = (right - left) / 2;
    int ry = (bottom - top) / 2;

    /* Use the smaller radius for a circle approximation */
    int r = rx < ry ? rx : ry;

    gdi_object_t *brush = get_gdi_obj((HGDIOBJ)dc->current_brush);
    uint32_t color = brush ? colorref_to_gfx(brush->brush_color) : GFX_RGB(0, 0, 0);

    /* Draw filled circle to the canvas */
    int cw, ch;
    uint32_t *canvas = wm_get_canvas(dc->wm_id, &cw, &ch);
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

    wm_mark_dirty();
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

    int sw, sh, dw, dh;
    uint32_t *src_canvas = wm_get_canvas(dc_table[si].wm_id, &sw, &sh);
    uint32_t *dst_canvas = wm_get_canvas(dc_table[di].wm_id, &dw, &dh);

    if (!src_canvas || !dst_canvas) return FALSE;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int sx = xSrc + x, sy = ySrc + y;
            int dx = xDest + x, dy = yDest + y;
            if (sx >= 0 && sx < sw && sy >= 0 && sy < sh &&
                dx >= 0 && dx < dw && dy >= 0 && dy < dh) {
                dst_canvas[dy * dw + dx] = src_canvas[sy * sw + sx];
            }
        }
    }

    wm_mark_dirty();
    return TRUE;
}

/* ── Stock Object Getter ─────────────────────────────────────── */

static HGDIOBJ WINAPI shim_GetStockObject(int i) {
    init_stock_objects();
    switch (i) {
        case 0: return (HGDIOBJ)(STOCK_WHITE_BRUSH + 1);   /* WHITE_BRUSH */
        case 4: return (HGDIOBJ)(STOCK_BLACK_BRUSH + 1);   /* BLACK_BRUSH */
        case 5: return (HGDIOBJ)(STOCK_NULL_BRUSH + 1);    /* NULL_BRUSH */
        case 13: return (HGDIOBJ)(STOCK_SYSTEM_FONT + 1);  /* SYSTEM_FONT */
        default: return (HGDIOBJ)0;
    }
}

/* ── Export Table ─────────────────────────────────────────────── */

static const win32_export_entry_t gdi32_exports[] = {
    { "GetDC",              (void *)shim_GetDC },
    { "ReleaseDC",          (void *)shim_ReleaseDC },
    { "BeginPaint",         (void *)shim_BeginPaint },
    { "EndPaint",           (void *)shim_EndPaint },
    { "TextOutA",           (void *)shim_TextOutA },
    { "SetPixel",           (void *)shim_SetPixel },
    { "GetPixel",           (void *)shim_GetPixel },
    { "CreateSolidBrush",   (void *)shim_CreateSolidBrush },
    { "FillRect",           (void *)shim_FillRect },
    { "SelectObject",       (void *)shim_SelectObject },
    { "DeleteObject",       (void *)shim_DeleteObject },
    { "CreateFontA",        (void *)shim_CreateFontA },
    { "SetTextColor",       (void *)shim_SetTextColor },
    { "SetBkColor",         (void *)shim_SetBkColor },
    { "SetBkMode",          (void *)shim_SetBkMode },
    { "Rectangle",          (void *)shim_Rectangle },
    { "Ellipse",            (void *)shim_Ellipse },
    { "BitBlt",             (void *)shim_BitBlt },
    { "GetStockObject",     (void *)shim_GetStockObject },
};

const win32_dll_shim_t win32_gdi32 = {
    .dll_name = "gdi32.dll",
    .exports = gdi32_exports,
    .num_exports = sizeof(gdi32_exports) / sizeof(gdi32_exports[0]),
};
