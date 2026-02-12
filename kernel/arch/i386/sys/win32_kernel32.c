#include <kernel/win32_types.h>
#include <kernel/fs.h>
#include <kernel/task.h>
#include <kernel/pmm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Win32 Error State ───────────────────────────────────────── */
static DWORD last_error = 0;

static void WINAPI shim_SetLastError(DWORD err) { last_error = err; }
static DWORD WINAPI shim_GetLastError(void) { return last_error; }

/* ── Handle Table ────────────────────────────────────────────── */
#define MAX_HANDLES 64

typedef enum {
    HTYPE_FREE = 0,
    HTYPE_FILE,
    HTYPE_CONSOLE,
    HTYPE_HEAP,
    HTYPE_PROCESS,
} handle_type_t;

typedef struct {
    handle_type_t type;
    char filename[64];
    uint8_t *buffer;
    size_t size;
    size_t pos;
} win32_handle_t;

static win32_handle_t handle_table[MAX_HANDLES];
static int handles_initialized = 0;

static void init_handles(void) {
    if (handles_initialized) return;
    memset(handle_table, 0, sizeof(handle_table));

    /* Reserve handles 0-2 for stdin/stdout/stderr */
    handle_table[0].type = HTYPE_CONSOLE;  /* stdin */
    handle_table[1].type = HTYPE_CONSOLE;  /* stdout */
    handle_table[2].type = HTYPE_CONSOLE;  /* stderr */

    handles_initialized = 1;
}

static HANDLE alloc_handle(handle_type_t type) {
    init_handles();
    for (int i = 3; i < MAX_HANDLES; i++) {
        if (handle_table[i].type == HTYPE_FREE) {
            handle_table[i].type = type;
            return (HANDLE)(i + 1);  /* 1-based to avoid NULL */
        }
    }
    return INVALID_HANDLE_VALUE;
}

static win32_handle_t *get_handle(HANDLE h) {
    init_handles();
    int idx = (int)h - 1;
    if (idx < 0 || idx >= MAX_HANDLES) return NULL;
    if (handle_table[idx].type == HTYPE_FREE) return NULL;
    return &handle_table[idx];
}

static void free_handle(HANDLE h) {
    int idx = (int)h - 1;
    if (idx < 3 || idx >= MAX_HANDLES) return;
    if (handle_table[idx].buffer) {
        free(handle_table[idx].buffer);
    }
    memset(&handle_table[idx], 0, sizeof(win32_handle_t));
}

/* ── File I/O ────────────────────────────────────────────────── */

static HANDLE WINAPI shim_CreateFileA(
    LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
    LPVOID lpSecurityAttributes, DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
    (void)dwDesiredAccess; (void)dwShareMode; (void)lpSecurityAttributes;
    (void)dwFlagsAndAttributes; (void)hTemplateFile;

    HANDLE h = alloc_handle(HTYPE_FILE);
    if (h == INVALID_HANDLE_VALUE) {
        last_error = 8; /* ERROR_NOT_ENOUGH_MEMORY */
        return INVALID_HANDLE_VALUE;
    }

    win32_handle_t *wh = get_handle(h);
    strncpy(wh->filename, lpFileName, sizeof(wh->filename) - 1);

    if (dwCreationDisposition == CREATE_ALWAYS || dwCreationDisposition == CREATE_NEW) {
        fs_create_file(lpFileName, 0);
        wh->buffer = malloc(MAX_FILE_SIZE);
        wh->size = 0;
        wh->pos = 0;
    } else {
        /* OPEN_EXISTING or OPEN_ALWAYS */
        wh->buffer = malloc(MAX_FILE_SIZE);
        if (!wh->buffer) {
            free_handle(h);
            return INVALID_HANDLE_VALUE;
        }
        wh->size = MAX_FILE_SIZE;
        if (fs_read_file(lpFileName, wh->buffer, &wh->size) < 0) {
            if (dwCreationDisposition == OPEN_ALWAYS) {
                fs_create_file(lpFileName, 0);
                wh->size = 0;
            } else {
                free_handle(h);
                last_error = 2; /* ERROR_FILE_NOT_FOUND */
                return INVALID_HANDLE_VALUE;
            }
        }
        wh->pos = 0;
    }

    return h;
}

static BOOL WINAPI shim_ReadFile(
    HANDLE hFile, LPVOID lpBuffer, DWORD nBytesToRead,
    LPDWORD lpBytesRead, LPVOID lpOverlapped)
{
    (void)lpOverlapped;
    win32_handle_t *wh = get_handle(hFile);
    if (!wh || wh->type != HTYPE_FILE) {
        last_error = 6; /* ERROR_INVALID_HANDLE */
        return FALSE;
    }

    DWORD avail = (wh->size > wh->pos) ? (DWORD)(wh->size - wh->pos) : 0;
    DWORD to_read = (nBytesToRead < avail) ? nBytesToRead : avail;

    if (to_read > 0) {
        memcpy(lpBuffer, wh->buffer + wh->pos, to_read);
        wh->pos += to_read;
    }

    if (lpBytesRead) *lpBytesRead = to_read;
    return TRUE;
}

static BOOL WINAPI shim_WriteFile(
    HANDLE hFile, LPCVOID lpBuffer, DWORD nBytesToWrite,
    LPDWORD lpBytesWritten, LPVOID lpOverlapped)
{
    (void)lpOverlapped;
    win32_handle_t *wh = get_handle(hFile);

    /* Console output */
    if (!wh || wh->type == HTYPE_CONSOLE) {
        const char *s = (const char *)lpBuffer;
        for (DWORD i = 0; i < nBytesToWrite; i++)
            putchar(s[i]);
        if (lpBytesWritten) *lpBytesWritten = nBytesToWrite;
        return TRUE;
    }

    if (wh->type != HTYPE_FILE) {
        last_error = 6;
        return FALSE;
    }

    /* Expand buffer if needed */
    DWORD end = wh->pos + nBytesToWrite;
    if (end > MAX_FILE_SIZE) {
        nBytesToWrite = MAX_FILE_SIZE - wh->pos;
        end = MAX_FILE_SIZE;
    }

    memcpy(wh->buffer + wh->pos, lpBuffer, nBytesToWrite);
    wh->pos += nBytesToWrite;
    if (wh->pos > wh->size) wh->size = wh->pos;

    if (lpBytesWritten) *lpBytesWritten = nBytesToWrite;
    return TRUE;
}

static BOOL WINAPI shim_CloseHandle(HANDLE hObject) {
    win32_handle_t *wh = get_handle(hObject);
    if (!wh) return FALSE;

    /* Flush file to FS on close */
    if (wh->type == HTYPE_FILE && wh->buffer && wh->size > 0) {
        fs_write_file(wh->filename, wh->buffer, wh->size);
    }

    free_handle(hObject);
    return TRUE;
}

static HANDLE WINAPI shim_GetStdHandle(DWORD nStdHandle) {
    init_handles();
    switch (nStdHandle) {
        case STD_INPUT_HANDLE:  return (HANDLE)1;  /* handle index 0 + 1 */
        case STD_OUTPUT_HANDLE: return (HANDLE)2;
        case STD_ERROR_HANDLE:  return (HANDLE)3;
        default: return INVALID_HANDLE_VALUE;
    }
}

/* ── Process / Module ────────────────────────────────────────── */

static void WINAPI shim_ExitProcess(UINT uExitCode) {
    printf("[Win32] ExitProcess(%u)\n", uExitCode);
    task_exit();
}

static HMODULE WINAPI shim_GetModuleHandleA(LPCSTR lpModuleName) {
    (void)lpModuleName;
    return (HMODULE)0x00400000;  /* Fake module base */
}

static void *WINAPI shim_GetProcAddress(HMODULE hModule, LPCSTR lpProcName) {
    (void)hModule;
    /* Try to resolve from our shim tables */
    void *p;
    p = win32_resolve_import("kernel32.dll", lpProcName);
    if (p) return p;
    p = win32_resolve_import("user32.dll", lpProcName);
    if (p) return p;
    p = win32_resolve_import("gdi32.dll", lpProcName);
    if (p) return p;
    p = win32_resolve_import("msvcrt.dll", lpProcName);
    if (p) return p;
    printf("[Win32] GetProcAddress: '%s' not found\n", lpProcName);
    return NULL;
}

static LPCSTR WINAPI shim_GetCommandLineA(void) {
    return "";
}

static DWORD WINAPI shim_GetCurrentProcessId(void) {
    int tid = task_get_current();
    return (DWORD)task_get_pid(tid);
}

static DWORD WINAPI shim_GetCurrentThreadId(void) {
    return shim_GetCurrentProcessId();
}

/* ── Memory ──────────────────────────────────────────────────── */

static LPVOID WINAPI shim_VirtualAlloc(
    LPVOID lpAddress, DWORD dwSize, DWORD flAllocationType, DWORD flProtect)
{
    (void)lpAddress; (void)flAllocationType; (void)flProtect;
    void *p = calloc(1, dwSize);
    return p;
}

static BOOL WINAPI shim_VirtualFree(LPVOID lpAddress, DWORD dwSize, DWORD dwFreeType) {
    (void)dwSize; (void)dwFreeType;
    free(lpAddress);
    return TRUE;
}

static HANDLE WINAPI shim_GetProcessHeap(void) {
    return (HANDLE)1;  /* Fake heap handle */
}

static HANDLE WINAPI shim_HeapCreate(DWORD flOptions, DWORD dwInitialSize, DWORD dwMaximumSize) {
    (void)flOptions; (void)dwInitialSize; (void)dwMaximumSize;
    return (HANDLE)1;
}

static LPVOID WINAPI shim_HeapAlloc(HANDLE hHeap, DWORD dwFlags, DWORD dwBytes) {
    (void)hHeap;
    void *p = malloc(dwBytes);
    if (p && (dwFlags & 0x08)) /* HEAP_ZERO_MEMORY */
        memset(p, 0, dwBytes);
    return p;
}

static BOOL WINAPI shim_HeapFree(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem) {
    (void)hHeap; (void)dwFlags;
    free(lpMem);
    return TRUE;
}

static LPVOID WINAPI shim_HeapReAlloc(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem, DWORD dwBytes) {
    (void)hHeap; (void)dwFlags;
    return realloc(lpMem, dwBytes);
}

static DWORD WINAPI shim_HeapSize(HANDLE hHeap, DWORD dwFlags, LPCVOID lpMem) {
    (void)hHeap; (void)dwFlags; (void)lpMem;
    return 0;  /* Can't determine — return 0 */
}

static BOOL WINAPI shim_HeapDestroy(HANDLE hHeap) {
    (void)hHeap;
    return TRUE;
}

/* ── Timing ──────────────────────────────────────────────────── */

extern uint32_t pit_get_ticks(void);
extern void pit_sleep_ms(uint32_t ms);

static DWORD WINAPI shim_GetTickCount(void) {
    /* PIT runs at 120Hz, so ticks * (1000/120) ≈ ticks * 8.33 */
    return (DWORD)(pit_get_ticks() * 1000 / 120);
}

static void WINAPI shim_Sleep(DWORD dwMilliseconds) {
    pit_sleep_ms(dwMilliseconds);
}

/* ── String / Misc ───────────────────────────────────────────── */

static int WINAPI shim_MultiByteToWideChar(
    UINT cp, DWORD flags, LPCSTR mb, int cbMB, void *wc, int cchWC)
{
    (void)cp; (void)flags;
    /* Simple ASCII → just copy bytes to 16-bit */
    int len = (cbMB < 0) ? (int)strlen(mb) + 1 : cbMB;
    if (cchWC == 0) return len;
    uint16_t *out = (uint16_t *)wc;
    int n = (len < cchWC) ? len : cchWC;
    for (int i = 0; i < n; i++)
        out[i] = (uint8_t)mb[i];
    return n;
}

static int WINAPI shim_WideCharToMultiByte(
    UINT cp, DWORD flags, const void *wc, int cchWC,
    LPSTR mb, int cbMB, LPCSTR defChar, LPVOID usedDef)
{
    (void)cp; (void)flags; (void)defChar; (void)usedDef;
    const uint16_t *in = (const uint16_t *)wc;
    int len = 0;
    if (cchWC < 0) {
        while (in[len]) len++;
        len++;
    } else {
        len = cchWC;
    }
    if (cbMB == 0) return len;
    int n = (len < cbMB) ? len : cbMB;
    for (int i = 0; i < n; i++)
        mb[i] = (char)(in[i] & 0xFF);
    return n;
}

static BOOL WINAPI shim_QueryPerformanceCounter(void *lpCounter) {
    if (lpCounter) {
        uint64_t *p = (uint64_t *)lpCounter;
        *p = (uint64_t)pit_get_ticks();
    }
    return TRUE;
}

static BOOL WINAPI shim_QueryPerformanceFrequency(void *lpFreq) {
    if (lpFreq) {
        uint64_t *p = (uint64_t *)lpFreq;
        *p = 120;  /* PIT frequency */
    }
    return TRUE;
}

/* Stub that just returns success for things we don't care about */
static BOOL WINAPI shim_stub_true(void) { return TRUE; }
static DWORD WINAPI shim_stub_zero(void) { return 0; }

/* ── Export Table ─────────────────────────────────────────────── */

static const win32_export_entry_t kernel32_exports[] = {
    /* File I/O */
    { "CreateFileA",            (void *)shim_CreateFileA },
    { "ReadFile",               (void *)shim_ReadFile },
    { "WriteFile",              (void *)shim_WriteFile },
    { "CloseHandle",            (void *)shim_CloseHandle },
    { "GetStdHandle",           (void *)shim_GetStdHandle },

    /* Process / Module */
    { "ExitProcess",            (void *)shim_ExitProcess },
    { "GetModuleHandleA",       (void *)shim_GetModuleHandleA },
    { "GetModuleHandleW",       (void *)shim_GetModuleHandleA },
    { "GetProcAddress",         (void *)shim_GetProcAddress },
    { "GetCommandLineA",        (void *)shim_GetCommandLineA },
    { "GetCommandLineW",        (void *)shim_GetCommandLineA },
    { "GetCurrentProcessId",    (void *)shim_GetCurrentProcessId },
    { "GetCurrentThreadId",     (void *)shim_GetCurrentThreadId },

    /* Memory */
    { "VirtualAlloc",           (void *)shim_VirtualAlloc },
    { "VirtualFree",            (void *)shim_VirtualFree },
    { "GetProcessHeap",         (void *)shim_GetProcessHeap },
    { "HeapCreate",             (void *)shim_HeapCreate },
    { "HeapAlloc",              (void *)shim_HeapAlloc },
    { "HeapFree",               (void *)shim_HeapFree },
    { "HeapReAlloc",            (void *)shim_HeapReAlloc },
    { "HeapSize",               (void *)shim_HeapSize },
    { "HeapDestroy",            (void *)shim_HeapDestroy },

    /* Timing */
    { "GetTickCount",           (void *)shim_GetTickCount },
    { "Sleep",                  (void *)shim_Sleep },
    { "QueryPerformanceCounter",    (void *)shim_QueryPerformanceCounter },
    { "QueryPerformanceFrequency",  (void *)shim_QueryPerformanceFrequency },

    /* Error */
    { "SetLastError",           (void *)shim_SetLastError },
    { "GetLastError",           (void *)shim_GetLastError },

    /* String conversion */
    { "MultiByteToWideChar",    (void *)shim_MultiByteToWideChar },
    { "WideCharToMultiByte",    (void *)shim_WideCharToMultiByte },

    /* Stubs — commonly imported but not critical */
    { "InitializeCriticalSection",  (void *)shim_stub_true },
    { "DeleteCriticalSection",      (void *)shim_stub_true },
    { "EnterCriticalSection",       (void *)shim_stub_true },
    { "LeaveCriticalSection",       (void *)shim_stub_true },
    { "TlsAlloc",                   (void *)shim_stub_zero },
    { "TlsFree",                    (void *)shim_stub_true },
    { "TlsGetValue",               (void *)shim_stub_zero },
    { "TlsSetValue",               (void *)shim_stub_true },
    { "FlsAlloc",                   (void *)shim_stub_zero },
    { "FlsFree",                    (void *)shim_stub_true },
    { "FlsGetValue",               (void *)shim_stub_zero },
    { "FlsSetValue",               (void *)shim_stub_true },
    { "IsProcessorFeaturePresent",  (void *)shim_stub_zero },
    { "IsDebuggerPresent",          (void *)shim_stub_zero },
    { "SetUnhandledExceptionFilter", (void *)shim_stub_zero },
    { "UnhandledExceptionFilter",   (void *)shim_stub_zero },
    { "GetSystemTimeAsFileTime",    (void *)shim_stub_true },
    { "GetStartupInfoA",            (void *)shim_stub_true },
    { "GetStartupInfoW",            (void *)shim_stub_true },
};

const win32_dll_shim_t win32_kernel32 = {
    .dll_name = "kernel32.dll",
    .exports = kernel32_exports,
    .num_exports = sizeof(kernel32_exports) / sizeof(kernel32_exports[0]),
};
