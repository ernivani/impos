#include <kernel/win32_types.h>
#include <kernel/wm.h>
#include <kernel/gfx.h>
#include <kernel/ui_event.h>
#include <kernel/mouse.h>
#include <kernel/task.h>
#include <kernel/io.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* From win32_gdi32.c — register HWND→wm_id mapping for GDI drawing */
extern void win32_gdi_register_hwnd(HWND hwnd, int wm_id);

/* Non-blocking keyboard input from libc */
extern int keyboard_getchar_nb(void);
extern int keyboard_get_ctrl(void);

/* ── Window Class Registry ───────────────────────────────────── */
#define MAX_WNDCLASSES 16

typedef struct {
    char class_name[64];
    WNDPROC wnd_proc;
    HBRUSH bg_brush;
    int registered;
} wndclass_entry_t;

static wndclass_entry_t wndclass_table[MAX_WNDCLASSES];

/* ── HWND → WM Window mapping ───────────────────────────────── */
#define MAX_WIN32_WINDOWS 16

typedef struct {
    int in_use;
    int wm_id;             /* ImposOS WM window id */
    WNDPROC wnd_proc;
    char class_name[64];
    char title[64];
    DWORD style;
    int x, y, w, h;       /* client area */
    HWND hwnd;             /* self-reference handle */
    int quit_posted;
    HBRUSH bg_brush;
} win32_window_t;

static win32_window_t win32_windows[MAX_WIN32_WINDOWS];
static int next_hwnd = 1;

static win32_window_t *hwnd_to_win(HWND hwnd) {
    for (int i = 0; i < MAX_WIN32_WINDOWS; i++) {
        if (win32_windows[i].in_use && win32_windows[i].hwnd == hwnd)
            return &win32_windows[i];
    }
    return NULL;
}

/* ── RegisterClassExA ────────────────────────────────────────── */

static ATOM WINAPI shim_RegisterClassExA(const WNDCLASSEXA *lpwcx) {
    if (!lpwcx || !lpwcx->lpszClassName) return 0;

    DBG("RegisterClassExA: class='%s' wndproc=0x%x", lpwcx->lpszClassName, (unsigned)lpwcx->lpfnWndProc);

    for (int i = 0; i < MAX_WNDCLASSES; i++) {
        if (!wndclass_table[i].registered) {
            strncpy(wndclass_table[i].class_name, lpwcx->lpszClassName, 63);
            wndclass_table[i].wnd_proc = lpwcx->lpfnWndProc;
            wndclass_table[i].bg_brush = lpwcx->hbrBackground;
            wndclass_table[i].registered = 1;
            DBG("RegisterClassExA: registered in slot %d → ATOM=%d", i, i + 1);
            return (ATOM)(i + 1);
        }
    }
    DBG("RegisterClassExA: FAILED - no free slots");
    return 0;
}

static wndclass_entry_t *find_class(LPCSTR className) {
    for (int i = 0; i < MAX_WNDCLASSES; i++) {
        if (wndclass_table[i].registered &&
            strcmp(wndclass_table[i].class_name, className) == 0)
            return &wndclass_table[i];
    }
    return NULL;
}

/* ── CreateWindowExA ─────────────────────────────────────────── */

static HWND WINAPI shim_CreateWindowExA(
    DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle,
    INT x, INT y, INT nWidth, INT nHeight,
    HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
{
    (void)dwExStyle; (void)hWndParent; (void)hMenu; (void)hInstance; (void)lpParam;

    wndclass_entry_t *cls = NULL;
    if (lpClassName)
        cls = find_class(lpClassName);

    /* Default size if CW_USEDEFAULT */
    if (x == CW_USEDEFAULT) x = 100;
    if (y == CW_USEDEFAULT) y = 100;
    if (nWidth == CW_USEDEFAULT) nWidth = 640;
    if (nHeight == CW_USEDEFAULT) nHeight = 480;

    /* Find a free slot */
    win32_window_t *win = NULL;
    for (int i = 0; i < MAX_WIN32_WINDOWS; i++) {
        if (!win32_windows[i].in_use) {
            win = &win32_windows[i];
            break;
        }
    }
    if (!win) return (HWND)0;

    memset(win, 0, sizeof(*win));
    win->in_use = 1;
    win->hwnd = (HWND)next_hwnd++;
    win->style = dwStyle;
    win->x = x;
    win->y = y;
    win->w = nWidth;
    win->h = nHeight;

    if (lpClassName)
        strncpy(win->class_name, lpClassName, 63);
    if (lpWindowName)
        strncpy(win->title, lpWindowName, 63);

    if (cls) {
        win->wnd_proc = cls->wnd_proc;
        win->bg_brush = cls->bg_brush;
    }

    /* Create the actual WM window */
    DBG("CreateWindowExA: creating WM window '%s' at (%d,%d) %dx%d",
        lpWindowName ? lpWindowName : "Win32", x, y, nWidth, nHeight);
    win->wm_id = wm_create_window(x, y, nWidth, nHeight,
                                   lpWindowName ? lpWindowName : "Win32");

    if (win->wm_id < 0) {
        DBG("CreateWindowExA: wm_create_window FAILED");
        win->in_use = 0;
        return (HWND)0;
    }

    /* Register HWND → wm_id mapping for GDI drawing */
    win32_gdi_register_hwnd(win->hwnd, win->wm_id);

    /* Link WM window to our task */
    int tid = task_get_current();
    task_info_t *task = task_get(tid);
    if (task) task->wm_id = win->wm_id;

    DBG("CreateWindowExA: '%s' → HWND=%u, WM_ID=%d, TID=%d",
        win->title, win->hwnd, win->wm_id, tid);

    /* Send WM_CREATE */
    if (win->wnd_proc)
        win->wnd_proc(win->hwnd, WM_CREATE, 0, 0);

    /* Show immediately if WS_VISIBLE */
    if (dwStyle & WS_VISIBLE)
        wm_focus_window(win->wm_id);

    return win->hwnd;
}

/* ── ShowWindow / UpdateWindow ───────────────────────────────── */

static BOOL WINAPI shim_ShowWindow(HWND hWnd, INT nCmdShow) {
    win32_window_t *win = hwnd_to_win(hWnd);
    if (!win) return FALSE;

    switch (nCmdShow) {
        case SW_HIDE:
            wm_minimize_window(win->wm_id);
            break;
        case SW_MAXIMIZE:
            wm_maximize_window(win->wm_id);
            break;
        case SW_MINIMIZE:
            wm_minimize_window(win->wm_id);
            break;
        default:
            wm_focus_window(win->wm_id);
            break;
    }
    return TRUE;
}

static BOOL WINAPI shim_UpdateWindow(HWND hWnd) {
    win32_window_t *win = hwnd_to_win(hWnd);
    if (!win) return FALSE;

    /* Send WM_PAINT directly */
    if (win->wnd_proc)
        win->wnd_proc(hWnd, WM_PAINT, 0, 0);

    wm_mark_dirty();
    return TRUE;
}

/* ── Message Loop ────────────────────────────────────────────── */

static BOOL WINAPI shim_GetMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax) {
    (void)hWnd; (void)wMsgFilterMin; (void)wMsgFilterMax;

    if (!lpMsg) return FALSE;

    /* Find the win32_window for this context */
    win32_window_t *win = NULL;
    for (int i = 0; i < MAX_WIN32_WINDOWS; i++) {
        if (win32_windows[i].in_use) {
            if (hWnd == 0 || win32_windows[i].hwnd == hWnd) {
                win = &win32_windows[i];
                break;
            }
        }
    }

    DBG("GetMessageA: enter hwnd=%u wm_id=%d quit=%d",
        win ? win->hwnd : 0, win ? win->wm_id : -1, win ? win->quit_posted : -1);

    /* No window found — all windows destroyed, act like WM_QUIT */
    if (!win) {
        DBG("GetMessageA: no window found, returning FALSE");
        return FALSE;
    }

    if (win->quit_posted) {
        DBG("GetMessageA: quit_posted=1, returning FALSE (WM_QUIT)");
        return FALSE;
    }

    /* Track previous mouse button state for edge detection */
    static uint8_t prev_buttons = 0;

    /* Non-blocking event poll — yields to other tasks each iteration.
     * Does NOT call ui_poll_event()/getchar() to avoid stealing
     * keyboard input from the desktop/shell. */
    while (1) {
        int tid = task_get_current();

        /* Check if task was killed (via kill command or signal) */
        if (task_check_killed(tid)) {
            DBG("GetMessageA: task %d killed, cleaning up", tid);
            if (win) {
                wm_destroy_window(win->wm_id);
                win->in_use = 0;
            }
            task_exit();
            return FALSE;  /* unreachable */
        }

        /* Check if window was closed by WM (destroyed externally) */
        if (win && wm_get_window(win->wm_id) == NULL) {
            DBG("GetMessageA: WM window %d destroyed externally → WM_CLOSE", win->wm_id);
            lpMsg->hwnd = win->hwnd;
            lpMsg->message = WM_CLOSE;
            lpMsg->wParam = 0;
            lpMsg->lParam = 0;
            return TRUE;
        }

        /* Check per-window close button (set by WM when close button clicked) */
        if (win) {
            wm_window_t *wmw = wm_get_window(win->wm_id);
            if (wmw && wmw->close_requested) {
                DBG("GetMessageA: close_requested on wm_id=%d → WM_CLOSE", win->wm_id);
                wmw->close_requested = 0;
                lpMsg->hwnd = win->hwnd;
                lpMsg->message = WM_CLOSE;
                lpMsg->wParam = 0;
                lpMsg->lParam = 0;
                return TRUE;
            }
        }

        /* Only consume keyboard/mouse when our window has focus */
        int has_focus = win && (wm_get_focused_id() == win->wm_id);

        if (has_focus) {
            /* Non-blocking keyboard check */
            int key = keyboard_getchar_nb();
            if (key) {
                lpMsg->hwnd = win ? win->hwnd : 0;
                lpMsg->message = WM_KEYDOWN;
                lpMsg->wParam = (WPARAM)key;
                lpMsg->lParam = 0;
                lpMsg->time = 0;
                lpMsg->pt.x = 0;
                lpMsg->pt.y = 0;
                return TRUE;
            }

            /* Non-blocking mouse check */
            uint8_t buttons = mouse_get_buttons();
            int mx = mouse_get_x();
            int my = mouse_get_y();

            if (buttons != prev_buttons) {
                uint8_t changed = buttons ^ prev_buttons;
                prev_buttons = buttons;

                if (changed & 1) {
                    lpMsg->hwnd = win ? win->hwnd : 0;
                    lpMsg->message = (buttons & 1) ? WM_LBUTTONDOWN : WM_LBUTTONUP;
                    lpMsg->wParam = 0;
                    lpMsg->lParam = (my << 16) | (mx & 0xFFFF);
                    lpMsg->time = 0;
                    lpMsg->pt.x = mx;
                    lpMsg->pt.y = my;
                    return TRUE;
                }
                if (changed & 2) {
                    lpMsg->hwnd = win ? win->hwnd : 0;
                    lpMsg->message = (buttons & 2) ? WM_RBUTTONDOWN : WM_RBUTTONUP;
                    lpMsg->wParam = 0;
                    lpMsg->lParam = (my << 16) | (mx & 0xFFFF);
                    lpMsg->time = 0;
                    lpMsg->pt.x = mx;
                    lpMsg->pt.y = my;
                    return TRUE;
                }
            }
        }

        /* Yield to other tasks */
        task_yield();

        /* Generate a WM_PAINT when window is dirty */
        {
            wm_window_t *wmw = wm_get_window(win->wm_id);
            if (wmw && wmw->dirty) {
                wmw->dirty = 0;  /* Clear before dispatching to avoid infinite paint loop */
                lpMsg->hwnd = win->hwnd;
                lpMsg->message = WM_PAINT;
                lpMsg->wParam = 0;
                lpMsg->lParam = 0;
                return TRUE;
            }
        }
    }
}

static BOOL WINAPI shim_TranslateMessage(const MSG *lpMsg) {
    /* Generate WM_CHAR from WM_KEYDOWN for printable characters */
    (void)lpMsg;
    return TRUE;
}

static LRESULT WINAPI shim_DispatchMessageA(const MSG *lpMsg) {
    if (!lpMsg) return 0;

    /* Only log non-paint messages to avoid spam */
    if (lpMsg->message != WM_PAINT)
        DBG("DispatchMessageA: hwnd=%u msg=0x%x", lpMsg->hwnd, lpMsg->message);

    win32_window_t *win = hwnd_to_win(lpMsg->hwnd);
    if (win && win->wnd_proc)
        return win->wnd_proc(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam);

    return 0;
}

static void WINAPI shim_PostQuitMessage(INT nExitCode) {
    DBG("PostQuitMessage: exitCode=%d", nExitCode);
    (void)nExitCode;
    for (int i = 0; i < MAX_WIN32_WINDOWS; i++) {
        if (win32_windows[i].in_use) {
            DBG("PostQuitMessage: setting quit_posted on hwnd=%u", win32_windows[i].hwnd);
            win32_windows[i].quit_posted = 1;
        }
    }
}

/* Forward declaration — needed by DefWindowProcA */
static BOOL WINAPI shim_DestroyWindow(HWND hWnd);

static LRESULT WINAPI shim_DefWindowProcA(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)wParam; (void)lParam;
    DBG("DefWindowProcA: hwnd=%u msg=0x%x", hWnd, msg);
    switch (msg) {
        case WM_CLOSE:
            DBG("DefWindowProcA: WM_CLOSE → calling DestroyWindow");
            shim_DestroyWindow(hWnd);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            return 0;
        default:
            return 0;
    }
}

/* ── MessageBox ──────────────────────────────────────────────── */

static INT WINAPI shim_MessageBoxA(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType) {
    (void)hWnd;

    printf("[MessageBox] %s: %s\n",
           lpCaption ? lpCaption : "(null)",
           lpText ? lpText : "(null)");

    /* For now, just auto-return OK/YES */
    if (uType & MB_YESNO) return IDYES;
    return IDOK;
}

/* ── Window Info ─────────────────────────────────────────────── */

static BOOL WINAPI shim_GetClientRect(HWND hWnd, LPRECT lpRect) {
    win32_window_t *win = hwnd_to_win(hWnd);
    if (!win || !lpRect) return FALSE;

    int cx, cy, cw, ch;
    wm_get_content_rect(win->wm_id, &cx, &cy, &cw, &ch);

    lpRect->left = 0;
    lpRect->top = 0;
    lpRect->right = cw;
    lpRect->bottom = ch;
    return TRUE;
}

static BOOL WINAPI shim_SetWindowTextA(HWND hWnd, LPCSTR lpString) {
    win32_window_t *win = hwnd_to_win(hWnd);
    if (!win) return FALSE;

    if (lpString)
        strncpy(win->title, lpString, 63);

    /* Update WM window title */
    wm_window_t *wmw = wm_get_window(win->wm_id);
    if (wmw && lpString)
        strncpy(wmw->title, lpString, 63);

    return TRUE;
}

static BOOL WINAPI shim_InvalidateRect(HWND hWnd, const RECT *lpRect, BOOL bErase) {
    (void)lpRect; (void)bErase;
    win32_window_t *win = hwnd_to_win(hWnd);
    if (win) {
        wm_window_t *wmw = wm_get_window(win->wm_id);
        if (wmw) wmw->dirty = 1;
        wm_mark_dirty();
    }
    return TRUE;
}

/* From win32_gdi32.c — unregister HWND mapping */
extern void win32_gdi_unregister_hwnd(HWND hwnd);

static BOOL WINAPI shim_DestroyWindow(HWND hWnd) {
    win32_window_t *win = hwnd_to_win(hWnd);
    if (!win) {
        DBG("DestroyWindow: hwnd=%u NOT FOUND", hWnd);
        return FALSE;
    }

    DBG("DestroyWindow: hwnd=%u wm_id=%d → sending WM_DESTROY", hWnd, win->wm_id);

    if (win->wnd_proc)
        win->wnd_proc(hWnd, WM_DESTROY, 0, 0);

    DBG("DestroyWindow: destroying WM window %d", win->wm_id);
    wm_destroy_window(win->wm_id);

    /* Free the window class slot used by this window */
    for (int i = 0; i < MAX_WNDCLASSES; i++) {
        if (wndclass_table[i].registered &&
            strcmp(wndclass_table[i].class_name, win->class_name) == 0) {
            wndclass_table[i].registered = 0;
            break;
        }
    }

    /* Free GDI HWND mapping */
    win32_gdi_unregister_hwnd(hWnd);

    win->in_use = 0;
    DBG("DestroyWindow: done, win->in_use=0");
    return TRUE;
}

static LRESULT WINAPI shim_SendMessageA(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    win32_window_t *win = hwnd_to_win(hWnd);
    if (win && win->wnd_proc)
        return win->wnd_proc(hWnd, msg, wParam, lParam);
    return 0;
}

static BOOL WINAPI shim_PostMessageA(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    /* Simplified: just send synchronously */
    shim_SendMessageA(hWnd, msg, wParam, lParam);
    return TRUE;
}

/* ── Misc Stubs ──────────────────────────────────────────────── */

static HCURSOR WINAPI shim_LoadCursorA(HINSTANCE hInst, LPCSTR lpCursorName) {
    (void)hInst; (void)lpCursorName;
    return (HCURSOR)1;
}

static HICON WINAPI shim_LoadIconA(HINSTANCE hInst, LPCSTR lpIconName) {
    (void)hInst; (void)lpIconName;
    return (HICON)1;
}

static int WINAPI shim_GetSystemMetrics(int nIndex) {
    switch (nIndex) {
        case 0:  return 1920;  /* SM_CXSCREEN */
        case 1:  return 1080;  /* SM_CYSCREEN */
        default: return 0;
    }
}

static BOOL WINAPI shim_SetTimer(HWND hWnd, UINT nIDEvent, UINT uElapse, void *lpTimerFunc) {
    (void)hWnd; (void)nIDEvent; (void)uElapse; (void)lpTimerFunc;
    return TRUE;  /* Stub — timers not fully implemented */
}

static BOOL WINAPI shim_KillTimer(HWND hWnd, UINT nIDEvent) {
    (void)hWnd; (void)nIDEvent;
    return TRUE;
}

/* ── Export Table ─────────────────────────────────────────────── */

static const win32_export_entry_t user32_exports[] = {
    { "RegisterClassExA",   (void *)shim_RegisterClassExA },
    { "CreateWindowExA",    (void *)shim_CreateWindowExA },
    { "ShowWindow",         (void *)shim_ShowWindow },
    { "UpdateWindow",       (void *)shim_UpdateWindow },
    { "GetMessageA",        (void *)shim_GetMessageA },
    { "TranslateMessage",   (void *)shim_TranslateMessage },
    { "DispatchMessageA",   (void *)shim_DispatchMessageA },
    { "DefWindowProcA",     (void *)shim_DefWindowProcA },
    { "PostQuitMessage",    (void *)shim_PostQuitMessage },
    { "MessageBoxA",        (void *)shim_MessageBoxA },
    { "GetClientRect",      (void *)shim_GetClientRect },
    { "SetWindowTextA",     (void *)shim_SetWindowTextA },
    { "InvalidateRect",     (void *)shim_InvalidateRect },
    { "DestroyWindow",      (void *)shim_DestroyWindow },
    { "SendMessageA",       (void *)shim_SendMessageA },
    { "PostMessageA",       (void *)shim_PostMessageA },
    { "LoadCursorA",        (void *)shim_LoadCursorA },
    { "LoadIconA",          (void *)shim_LoadIconA },
    { "GetSystemMetrics",   (void *)shim_GetSystemMetrics },
    { "SetTimer",           (void *)shim_SetTimer },
    { "KillTimer",          (void *)shim_KillTimer },
};

const win32_dll_shim_t win32_user32 = {
    .dll_name = "user32.dll",
    .exports = user32_exports,
    .num_exports = sizeof(user32_exports) / sizeof(user32_exports[0]),
};
