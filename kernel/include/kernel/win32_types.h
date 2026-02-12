#ifndef _KERNEL_WIN32_TYPES_H
#define _KERNEL_WIN32_TYPES_H

#include <stdint.h>

/* ── Basic Windows Types ─────────────────────────────────────── */
typedef uint32_t DWORD;
typedef uint16_t WORD;
typedef uint8_t  BYTE;
typedef int32_t  LONG;
typedef int32_t  INT;
typedef uint32_t UINT;
typedef int      BOOL;
typedef char     CHAR;
typedef uint16_t WCHAR;
typedef void     VOID;
typedef void    *PVOID;
typedef void    *LPVOID;
typedef const void *LPCVOID;
typedef char    *LPSTR;
typedef const char *LPCSTR;
typedef DWORD   *LPDWORD;
typedef BYTE    *LPBYTE;

/* Handles */
typedef uint32_t HANDLE;
typedef uint32_t HWND;
typedef uint32_t HDC;
typedef uint32_t HBRUSH;
typedef uint32_t HFONT;
typedef uint32_t HPEN;
typedef uint32_t HBITMAP;
typedef uint32_t HGDIOBJ;
typedef uint32_t HINSTANCE;
typedef uint32_t HMENU;
typedef uint32_t HICON;
typedef uint32_t HCURSOR;
typedef uint32_t HMODULE;
typedef uint32_t ATOM;

/* Special values */
#define INVALID_HANDLE_VALUE  ((HANDLE)0xFFFFFFFF)
#define NULL_HANDLE           ((HANDLE)0)

/* BOOL values */
#ifndef TRUE
#define TRUE    1
#endif
#ifndef FALSE
#define FALSE   0
#endif

/* Calling conventions (no-op on our flat model) */
#define WINAPI   __attribute__((cdecl))
#define CALLBACK __attribute__((cdecl))
#define APIENTRY __attribute__((cdecl))

/* Callback types */
typedef uint32_t (CALLBACK *WNDPROC)(HWND, UINT, uint32_t, uint32_t);
typedef uint32_t WPARAM;
typedef uint32_t LPARAM;
typedef uint32_t LRESULT;

/* ── Structures ──────────────────────────────────────────────── */
typedef struct {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT;
typedef RECT *LPRECT;

typedef struct {
    LONG x;
    LONG y;
} POINT;
typedef POINT *LPPOINT;

typedef struct {
    HWND   hwnd;
    UINT   message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD  time;
    POINT  pt;
} MSG;
typedef MSG *LPMSG;

typedef struct {
    HDC  hdc;
    BOOL fErase;
    RECT rcPaint;
    BOOL fRestore;
    BOOL fIncUpdate;
    BYTE rgbReserved[32];
} PAINTSTRUCT;
typedef PAINTSTRUCT *LPPAINTSTRUCT;

typedef struct {
    UINT      cbSize;
    UINT      style;
    WNDPROC   lpfnWndProc;
    INT       cbClsExtra;
    INT       cbWndExtra;
    HINSTANCE hInstance;
    HICON     hIcon;
    HCURSOR   hCursor;
    HBRUSH    hbrBackground;
    LPCSTR    lpszMenuName;
    LPCSTR    lpszClassName;
    HICON     hIconSm;
} WNDCLASSEXA;

/* For CreateWindowEx */
typedef struct {
    LPVOID    lpCreateParams;
    HINSTANCE hInstance;
    HMENU     hMenu;
    HWND      hwndParent;
    INT       cy;
    INT       cx;
    INT       y;
    INT       x;
    LONG      style;
    LPCSTR    lpszName;
    LPCSTR    lpszClass;
    DWORD     dwExStyle;
} CREATESTRUCTA;

/* ── Windows Constants ───────────────────────────────────────── */

/* Window messages */
#define WM_NULL         0x0000
#define WM_CREATE       0x0001
#define WM_DESTROY      0x0002
#define WM_MOVE         0x0003
#define WM_SIZE         0x0005
#define WM_SETFOCUS     0x0007
#define WM_KILLFOCUS    0x0008
#define WM_CLOSE        0x0010
#define WM_QUIT         0x0012
#define WM_PAINT        0x000F
#define WM_ERASEBKGND   0x0014
#define WM_KEYDOWN      0x0100
#define WM_KEYUP        0x0101
#define WM_CHAR         0x0102
#define WM_COMMAND      0x0111
#define WM_TIMER        0x0113
#define WM_MOUSEMOVE    0x0200
#define WM_LBUTTONDOWN  0x0201
#define WM_LBUTTONUP    0x0202
#define WM_RBUTTONDOWN  0x0204
#define WM_RBUTTONUP    0x0205

/* Window styles */
#define WS_OVERLAPPED   0x00000000
#define WS_POPUP        0x80000000
#define WS_CHILD        0x40000000
#define WS_MINIMIZE     0x20000000
#define WS_VISIBLE      0x10000000
#define WS_CAPTION      0x00C00000
#define WS_BORDER       0x00800000
#define WS_SYSMENU      0x00080000
#define WS_THICKFRAME   0x00040000
#define WS_MINIMIZEBOX  0x00020000
#define WS_MAXIMIZEBOX  0x00010000
#define WS_OVERLAPPEDWINDOW (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)

/* Extended window styles */
#define WS_EX_CLIENTEDGE  0x00000200

/* ShowWindow commands */
#define SW_HIDE         0
#define SW_SHOWNORMAL   1
#define SW_SHOW         5
#define SW_MINIMIZE     6
#define SW_MAXIMIZE     3

/* MessageBox types */
#define MB_OK               0x00000000
#define MB_OKCANCEL          0x00000001
#define MB_YESNO             0x00000004
#define MB_ICONERROR         0x00000010
#define MB_ICONWARNING       0x00000030
#define MB_ICONINFORMATION   0x00000040

/* MessageBox return values */
#define IDOK     1
#define IDCANCEL 2
#define IDYES    6
#define IDNO     7

/* CW_USEDEFAULT */
#define CW_USEDEFAULT  ((INT)0x80000000)

/* Class styles */
#define CS_HREDRAW      0x0002
#define CS_VREDRAW      0x0001

/* Color constants */
#define COLOR_WINDOW    5
#define COLOR_BTNFACE   15

/* GDI constants */
#define TRANSPARENT     1
#define OPAQUE          2
#define SRCCOPY         0x00CC0020

/* Virtual key codes */
#define VK_BACK     0x08
#define VK_TAB      0x09
#define VK_RETURN   0x0D
#define VK_ESCAPE   0x1B
#define VK_SPACE    0x20
#define VK_LEFT     0x25
#define VK_UP       0x26
#define VK_RIGHT    0x27
#define VK_DOWN     0x28
#define VK_DELETE   0x2E
#define VK_0        0x30
#define VK_9        0x39
#define VK_A        0x41
#define VK_Z        0x5A

/* Standard handles */
#define STD_INPUT_HANDLE  ((DWORD)-10)
#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define STD_ERROR_HANDLE  ((DWORD)-12)

/* File access */
#define GENERIC_READ    0x80000000
#define GENERIC_WRITE   0x40000000
#define FILE_SHARE_READ 0x00000001

/* CreateFile dispositions */
#define CREATE_NEW        1
#define CREATE_ALWAYS     2
#define OPEN_EXISTING     3
#define OPEN_ALWAYS       4
#define TRUNCATE_EXISTING 5

/* MEM allocation types */
#define MEM_COMMIT    0x1000
#define MEM_RESERVE   0x2000
#define MEM_RELEASE   0x8000
#define PAGE_READWRITE 0x04

/* COLORREF */
typedef DWORD COLORREF;
#define RGB(r,g,b) ((COLORREF)(((BYTE)(r)|((WORD)((BYTE)(g))<<8))|(((DWORD)(BYTE)(b))<<16)))
#define GetRValue(c) ((BYTE)(c))
#define GetGValue(c) ((BYTE)(((WORD)(c))>>8))
#define GetBValue(c) ((BYTE)((c)>>16))

/* ── Win32 shim DLL lookup ───────────────────────────────────── */
typedef struct {
    const char *name;
    void       *func;
} win32_export_entry_t;

typedef struct {
    const char            *dll_name;
    const win32_export_entry_t *exports;
    int                    num_exports;
} win32_dll_shim_t;

/* Each shim module registers itself */
extern const win32_dll_shim_t win32_kernel32;
extern const win32_dll_shim_t win32_user32;
extern const win32_dll_shim_t win32_gdi32;
extern const win32_dll_shim_t win32_msvcrt;

/* Master lookup: find a function by DLL name + function name */
void *win32_resolve_import(const char *dll_name, const char *func_name);

#endif
