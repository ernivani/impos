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
typedef uint32_t ULONG;
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

/* Calling conventions — Win32 API functions use stdcall (callee cleans stack).
 * This must match what PE apps compiled with mingw expect. */
#define WINAPI   __attribute__((stdcall))
#define CALLBACK __attribute__((stdcall))
#define APIENTRY __attribute__((stdcall))

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
#define MEM_FREE      0x10000

/* Page protection */
#define PAGE_NOACCESS          0x01
#define PAGE_READONLY          0x02
#define PAGE_READWRITE         0x04
#define PAGE_EXECUTE_READ      0x20
#define PAGE_EXECUTE_READWRITE 0x40

/* Wait constants */
#define WAIT_OBJECT_0   0x00000000
#define WAIT_TIMEOUT    0x00000102
#define WAIT_FAILED     0xFFFFFFFF
#define INFINITE        0xFFFFFFFF

/* Thread creation flags */
#define CREATE_SUSPENDED 0x00000004

/* Event flags */
#define EVENT_MODIFY_STATE 0x0002

/* Synchronization */
typedef struct {
    volatile LONG LockCount;
    LONG          RecursionCount;
    DWORD         OwningThread;
    DWORD         SpinCount;
} CRITICAL_SECTION, *LPCRITICAL_SECTION;

/* Security attributes stub */
typedef struct {
    DWORD  nLength;
    LPVOID lpSecurityDescriptor;
    BOOL   bInheritHandle;
} SECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;

/* Thread entry point */
typedef DWORD (WINAPI *LPTHREAD_START_ROUTINE)(LPVOID);

/* Large integer for QueryPerformanceCounter */
typedef union {
    struct { DWORD LowPart; LONG HighPart; } u;
    int64_t QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

/* Handle types (additional) */
typedef uint32_t HRGN;

/* COLORREF */
typedef DWORD COLORREF;
#define RGB(r,g,b) ((COLORREF)(((BYTE)(r)|((WORD)((BYTE)(g))<<8))|(((DWORD)(BYTE)(b))<<16)))
#define GetRValue(c) ((BYTE)(c))
#define GetGValue(c) ((BYTE)(((WORD)(c))>>8))
#define GetBValue(c) ((BYTE)((c)>>16))

/* ── SIZE ────────────────────────────────────────────────────── */
typedef struct {
    LONG cx;
    LONG cy;
} SIZE, *LPSIZE;

/* ── TEXTMETRICA ─────────────────────────────────────────────── */
typedef struct {
    LONG tmHeight;
    LONG tmAscent;
    LONG tmDescent;
    LONG tmInternalLeading;
    LONG tmExternalLeading;
    LONG tmAveCharWidth;
    LONG tmMaxCharWidth;
    LONG tmWeight;
    LONG tmOverhang;
    LONG tmDigitizedAspectX;
    LONG tmDigitizedAspectY;
    BYTE tmFirstChar;
    BYTE tmLastChar;
    BYTE tmDefaultChar;
    BYTE tmBreakChar;
    BYTE tmItalic;
    BYTE tmUnderlined;
    BYTE tmStruckOut;
    BYTE tmPitchAndFamily;
    BYTE tmCharSet;
} TEXTMETRICA, *LPTEXTMETRICA;

/* ── LOGFONTA ────────────────────────────────────────────────── */
typedef struct {
    LONG  lfHeight;
    LONG  lfWidth;
    LONG  lfEscapement;
    LONG  lfOrientation;
    LONG  lfWeight;
    BYTE  lfItalic;
    BYTE  lfUnderline;
    BYTE  lfStrikeOut;
    BYTE  lfCharSet;
    BYTE  lfOutPrecision;
    BYTE  lfClipPrecision;
    BYTE  lfQuality;
    BYTE  lfPitchAndFamily;
    CHAR  lfFaceName[32];
} LOGFONTA, *LPLOGFONTA;

/* ── BITMAP (for GetObjectA) ─────────────────────────────────── */
typedef struct {
    LONG   bmType;
    LONG   bmWidth;
    LONG   bmHeight;
    LONG   bmWidthBytes;
    WORD   bmPlanes;
    WORD   bmBitsPixel;
    LPVOID bmBits;
} BITMAP, *LPBITMAP;

/* ── BITMAPINFOHEADER / BITMAPINFO ───────────────────────────── */
typedef struct {
    DWORD biSize;
    LONG  biWidth;
    LONG  biHeight;
    WORD  biPlanes;
    WORD  biBitCount;
    DWORD biCompression;
    DWORD biSizeImage;
    LONG  biXPelsPerMeter;
    LONG  biYPelsPerMeter;
    DWORD biClrUsed;
    DWORD biClrImportant;
} BITMAPINFOHEADER;

typedef struct {
    DWORD rgbBlue;
    DWORD rgbGreen;
    DWORD rgbRed;
    DWORD rgbReserved;
} RGBQUAD;

typedef struct {
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD          bmiColors[1];
} BITMAPINFO, *LPBITMAPINFO;

/* ── ENUMLOGFONTEXA (for EnumFontFamiliesExA callback) ───────── */
typedef struct {
    LOGFONTA elfLogFont;
    CHAR     elfFullName[64];
    CHAR     elfStyle[32];
    CHAR     elfScript[32];
} ENUMLOGFONTEXA;

typedef struct {
    TEXTMETRICA ntmTm;
    DWORD       ntmFlags;
    DWORD       ntmSizeEM;
    DWORD       ntmCellHeight;
    DWORD       ntmAvgWidth;
} NEWTEXTMETRICEXA;

typedef int (CALLBACK *FONTENUMPROCA)(const ENUMLOGFONTEXA *, const NEWTEXTMETRICEXA *, DWORD, LPARAM);

/* ── GDI constants (additional) ──────────────────────────────── */
#define PS_SOLID        0
#define PS_DASH         1
#define PS_DOT          2
#define PS_NULL         5

#define DIB_RGB_COLORS  0
#define BI_RGB          0

/* GetDeviceCaps indices */
#define HORZRES         8
#define VERTRES         10
#define BITSPIXEL       12
#define PLANES          14
#define LOGPIXELSX      88
#define LOGPIXELSY      90
#define SIZEPALETTE     104
#define NUMCOLORS       24
#define RASTERCAPS      38
#define TECHNOLOGY      2
#define DT_RASDISPLAY   1

/* Raster ops */
#define SRCINVERT       0x00660046
#define SRCAND          0x008800C6
#define SRCPAINT        0x00EE0086
#define BLACKNESS       0x00000042
#define WHITENESS       0x00FF0062

/* Stock objects additional */
#define WHITE_PEN       6
#define BLACK_PEN       7
#define NULL_PEN        8
#define DEFAULT_PALETTE 15

/* ── Wide String Types ───────────────────────────────────────── */
typedef WCHAR  *LPWSTR;
typedef const WCHAR *LPCWSTR;

/* ── COM / OLE Types ────────────────────────────────────────── */
typedef uint32_t HRESULT;
typedef uint32_t HGLOBAL;

/* GUID / IID / CLSID */
typedef struct { DWORD Data1; WORD Data2; WORD Data3; BYTE Data4[8]; } GUID;
typedef GUID IID;
typedef GUID CLSID;
#define REFCLSID const CLSID *
#define REFIID   const IID *

/* HRESULT constants */
#define S_OK          ((HRESULT)0x00000000)
#define S_FALSE       ((HRESULT)0x00000001)
#define E_NOINTERFACE ((HRESULT)0x80004002)
#define E_POINTER     ((HRESULT)0x80004003)
#define E_NOTIMPL     ((HRESULT)0x80004001)
#define E_FAIL        ((HRESULT)0x80004005)
#define E_OUTOFMEMORY ((HRESULT)0x8007000E)
#define CLASS_E_CLASSNOTAVAILABLE ((HRESULT)0x80040111)
#define REGDB_E_CLASSNOTREG      ((HRESULT)0x80040154)
#define SUCCEEDED(hr) (((int32_t)(hr)) >= 0)
#define FAILED(hr)    (((int32_t)(hr)) < 0)

/* COM calling convention */
#define STDMETHODCALLTYPE WINAPI

/* CSIDL constants for SHGetFolderPath */
#define CSIDL_DESKTOP           0x0000
#define CSIDL_PROGRAMS          0x0002
#define CSIDL_PERSONAL          0x0005
#define CSIDL_APPDATA           0x001A
#define CSIDL_LOCAL_APPDATA     0x001C
#define CSIDL_COMMON_APPDATA    0x0023
#define CSIDL_WINDOWS           0x0024
#define CSIDL_SYSTEM            0x0025
#define CSIDL_PROGRAM_FILES     0x0026

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

/* ── W-suffix Structs ───────────────────────────────────────── */
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
    LPCWSTR   lpszMenuName;
    LPCWSTR   lpszClassName;
    HICON     hIconSm;
} WNDCLASSEXW;

typedef struct {
    LONG  lfHeight;
    LONG  lfWidth;
    LONG  lfEscapement;
    LONG  lfOrientation;
    LONG  lfWeight;
    BYTE  lfItalic;
    BYTE  lfUnderline;
    BYTE  lfStrikeOut;
    BYTE  lfCharSet;
    BYTE  lfOutPrecision;
    BYTE  lfClipPrecision;
    BYTE  lfQuality;
    BYTE  lfPitchAndFamily;
    WCHAR lfFaceName[32];
} LOGFONTW, *LPLOGFONTW;

typedef struct {
    DWORD    dwFileAttributes;
    DWORD    ftCreationTime[2];
    DWORD    ftLastAccessTime[2];
    DWORD    ftLastWriteTime[2];
    DWORD    nFileSizeHigh;
    DWORD    nFileSizeLow;
    DWORD    dwReserved0;
    DWORD    dwReserved1;
    WCHAR    cFileName[260];
    WCHAR    cAlternateFileName[14];
} WIN32_FIND_DATAW, *LPWIN32_FIND_DATAW;

/* ── UTF-8 ↔ UTF-16 helpers (implemented in win32_kernel32.c) ── */
int win32_utf8_to_wchar(const char *utf8, int utf8_len, WCHAR *out, int out_len);
int win32_wchar_to_utf8(const WCHAR *wstr, int wstr_len, char *out, int out_len);

/* ── Structured Exception Handling ────────────────────────────── */

/* Exception codes */
#define EXCEPTION_ACCESS_VIOLATION          0xC0000005
#define EXCEPTION_ARRAY_BOUNDS_EXCEEDED     0xC000008C
#define EXCEPTION_BREAKPOINT                0x80000003
#define EXCEPTION_DATATYPE_MISALIGNMENT     0x80000002
#define EXCEPTION_FLT_DIVIDE_BY_ZERO        0xC000008E
#define EXCEPTION_FLT_OVERFLOW              0xC0000091
#define EXCEPTION_ILLEGAL_INSTRUCTION       0xC000001D
#define EXCEPTION_INT_DIVIDE_BY_ZERO        0xC0000094
#define EXCEPTION_INT_OVERFLOW              0xC0000095
#define EXCEPTION_NONCONTINUABLE_EXCEPTION  0xC0000025
#define EXCEPTION_PRIV_INSTRUCTION          0xC0000096
#define EXCEPTION_SINGLE_STEP              0x80000004
#define EXCEPTION_STACK_OVERFLOW            0xC00000FD
#define STATUS_UNWIND                       0xC0000027

/* C++ exception code (MSVC magic) */
#define EXCEPTION_MSVC_CPP                  0xE06D7363

/* Exception flags */
#define EXCEPTION_NONCONTINUABLE    0x01
#define EXCEPTION_UNWINDING         0x02
#define EXCEPTION_EXIT_UNWIND       0x04

/* Exception filter return values */
#define EXCEPTION_EXECUTE_HANDLER     1
#define EXCEPTION_CONTINUE_SEARCH     0
#define EXCEPTION_CONTINUE_EXECUTION (-1)

#define EXCEPTION_MAXIMUM_PARAMETERS 15

/* SEH chain end sentinel */
#define SEH_CHAIN_END  0xFFFFFFFF

/* Exception disposition (return from handler) */
typedef enum {
    ExceptionContinueExecution = 0,
    ExceptionContinueSearch    = 1,
    ExceptionNestedException   = 2,
    ExceptionCollidedUnwind    = 3
} EXCEPTION_DISPOSITION;

/* EXCEPTION_RECORD — describes the exception */
typedef struct _EXCEPTION_RECORD {
    DWORD ExceptionCode;
    DWORD ExceptionFlags;
    struct _EXCEPTION_RECORD *ExceptionRecord;
    PVOID ExceptionAddress;
    DWORD NumberParameters;
    DWORD ExceptionInformation[EXCEPTION_MAXIMUM_PARAMETERS];
} EXCEPTION_RECORD, *PEXCEPTION_RECORD;

/* CONTEXT — i386 register state */
#define CONTEXT_i386               0x00010000
#define CONTEXT_CONTROL            (CONTEXT_i386 | 0x01)
#define CONTEXT_INTEGER            (CONTEXT_i386 | 0x02)
#define CONTEXT_SEGMENTS           (CONTEXT_i386 | 0x04)
#define CONTEXT_FULL               (CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_SEGMENTS)

typedef struct {
    DWORD ContextFlags;
    /* Debug registers (DR0-DR3, DR6, DR7) */
    DWORD Dr0, Dr1, Dr2, Dr3, Dr6, Dr7;
    /* Floating point (stub — no FPU state for now) */
    BYTE  FloatSave[112];
    /* Segment registers */
    DWORD SegGs, SegFs, SegEs, SegDs;
    /* Integer registers (from PUSHA order) */
    DWORD Edi, Esi, Ebx, Edx, Ecx, Eax;
    /* Control registers */
    DWORD Ebp, Eip, SegCs, EFlags, Esp, SegSs;
} CONTEXT, *PCONTEXT, *LPCONTEXT;

/* EXCEPTION_POINTERS — passed to exception filters */
typedef struct {
    PEXCEPTION_RECORD ExceptionRecord;
    PCONTEXT          ContextRecord;
} EXCEPTION_POINTERS, *PEXCEPTION_POINTERS, *LPEXCEPTION_POINTERS;

/* Top-level exception filter */
typedef LONG (*LPTOP_LEVEL_EXCEPTION_FILTER)(EXCEPTION_POINTERS *);

/* SEH registration record (linked list via FS:[0]) */
typedef struct _EXCEPTION_REGISTRATION_RECORD {
    struct _EXCEPTION_REGISTRATION_RECORD *Next;
    PVOID Handler;
} EXCEPTION_REGISTRATION_RECORD;

/* NT_TIB — Thread Information Block (first part of TEB) */
typedef struct _NT_TIB {
    uint32_t ExceptionList;   /* offset 0x00: ptr to EXCEPTION_REGISTRATION_RECORD */
    uint32_t StackBase;       /* offset 0x04 */
    uint32_t StackLimit;      /* offset 0x08 */
    uint32_t SubSystemTib;    /* offset 0x0C */
    uint32_t FiberData;       /* offset 0x10 */
    uint32_t ArbitraryUser;   /* offset 0x14 */
    uint32_t Self;            /* offset 0x18: linear address of this TIB */
} NT_TIB;

/* WIN32_TEB — Thread Environment Block, padded to 4KB */
typedef struct {
    NT_TIB   tib;             /* offset 0x00 */
    uint32_t EnvironmentPtr;  /* offset 0x1C */
    uint32_t ClientId[2];     /* offset 0x20: ProcessId, ThreadId */
    uint32_t Reserved1[2];    /* offset 0x28 */
    uint32_t LastError;       /* offset 0x30: GetLastError() value */
    uint8_t  _pad[4096 - 0x34];
} __attribute__((packed)) WIN32_TEB;

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
extern const win32_dll_shim_t win32_advapi32;
extern const win32_dll_shim_t win32_ws2_32;
extern const win32_dll_shim_t win32_gdiplus;
extern const win32_dll_shim_t win32_ole32;
extern const win32_dll_shim_t win32_shell32;
extern const win32_dll_shim_t win32_bcrypt;
extern const win32_dll_shim_t win32_crypt32;

/* Registry init (call early to pre-populate keys) */
void registry_init(void);

/* Master lookup: find a function by DLL name + function name */
void *win32_resolve_import(const char *dll_name, const char *func_name);

#endif
