#include <kernel/win32_types.h>
#include <kernel/fs.h>
#include <kernel/task.h>
#include <kernel/pmm.h>
#include <kernel/io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Win32 Error State ───────────────────────────────────────── */
static DWORD last_error = 0;

static void WINAPI shim_SetLastError(DWORD err) { last_error = err; }
static DWORD WINAPI shim_GetLastError(void) { return last_error; }

/* ── Handle Table ────────────────────────────────────────────── */
#define MAX_HANDLES 128

typedef enum {
    HTYPE_FREE = 0,
    HTYPE_FILE,
    HTYPE_CONSOLE,
    HTYPE_HEAP,
    HTYPE_PROCESS,
    HTYPE_THREAD,
    HTYPE_EVENT,
    HTYPE_MUTEX,
    HTYPE_SEMAPHORE,
} handle_type_t;

typedef struct {
    handle_type_t type;
    char filename[64];
    uint8_t *buffer;
    size_t size;
    size_t pos;

    /* Thread state */
    int             tid;           /* task slot for HTYPE_THREAD */
    DWORD           thread_exit;   /* exit code */
    volatile int    thread_done;   /* 1 when thread finished */

    /* Event state */
    volatile int    signaled;
    int             manual_reset;

    /* Mutex state */
    volatile DWORD  mutex_owner;   /* owning thread id, 0 = unowned */
    volatile int    mutex_count;   /* recursion count */

    /* Semaphore state */
    volatile LONG   sem_count;
    LONG            sem_max;
} win32_handle_t;

static win32_handle_t handle_table[MAX_HANDLES];
static int handles_initialized = 0;

static void init_handles(void) {
    if (handles_initialized) return;
    memset(handle_table, 0, sizeof(handle_table));

    /* Reserve handles 0-2 for stdin/stdout/stderr */
    handle_table[0].type = HTYPE_CONSOLE;
    handle_table[1].type = HTYPE_CONSOLE;
    handle_table[2].type = HTYPE_CONSOLE;

    handles_initialized = 1;
}

static HANDLE alloc_handle(handle_type_t type) {
    init_handles();
    for (int i = 3; i < MAX_HANDLES; i++) {
        if (handle_table[i].type == HTYPE_FREE) {
            memset(&handle_table[i], 0, sizeof(win32_handle_t));
            handle_table[i].type = type;
            return (HANDLE)(i + 1);
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
        last_error = 8;
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
                last_error = 2;
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
        last_error = 6;
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

    if (wh->type == HTYPE_FILE && wh->buffer && wh->size > 0) {
        fs_write_file(wh->filename, wh->buffer, wh->size);
    }

    free_handle(hObject);
    return TRUE;
}

static HANDLE WINAPI shim_GetStdHandle(DWORD nStdHandle) {
    init_handles();
    switch (nStdHandle) {
        case STD_INPUT_HANDLE:  return (HANDLE)1;
        case STD_OUTPUT_HANDLE: return (HANDLE)2;
        case STD_ERROR_HANDLE:  return (HANDLE)3;
        default: return INVALID_HANDLE_VALUE;
    }
}

/* ── Process / Module ────────────────────────────────────────── */

static void WINAPI shim_ExitProcess(UINT uExitCode) {
    DBG("ExitProcess(%u)", uExitCode);
    task_exit();
}

static HMODULE WINAPI shim_GetModuleHandleA(LPCSTR lpModuleName) {
    (void)lpModuleName;
    return (HMODULE)0x00400000;
}

static void *WINAPI shim_GetProcAddress(HMODULE hModule, LPCSTR lpProcName) {
    (void)hModule;
    void *p;
    p = win32_resolve_import("kernel32.dll", lpProcName);
    if (p) return p;
    p = win32_resolve_import("user32.dll", lpProcName);
    if (p) return p;
    p = win32_resolve_import("gdi32.dll", lpProcName);
    if (p) return p;
    p = win32_resolve_import("msvcrt.dll", lpProcName);
    if (p) return p;
    DBG("GetProcAddress: '%s' not found", lpProcName);
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
    return (DWORD)task_get_current();
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
    return (HANDLE)1;
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
    return 0;
}

static BOOL WINAPI shim_HeapDestroy(HANDLE hHeap) {
    (void)hHeap;
    return TRUE;
}

/* ── Timing ──────────────────────────────────────────────────── */

extern uint32_t pit_get_ticks(void);
extern void pit_sleep_ms(uint32_t ms);

static DWORD WINAPI shim_GetTickCount(void) {
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
        *p = 120;
    }
    return TRUE;
}

/* ═══════════════════════════════════════════════════════════════
 *  PHASE 1: Threading & Synchronization
 * ═══════════════════════════════════════════════════════════════ */

/* ── CreateThread ────────────────────────────────────────────── */

/* Per-thread context for Win32 threads */
typedef struct {
    LPTHREAD_START_ROUTINE start;
    LPVOID param;
    HANDLE handle;  /* handle index so thread can set exit code */
} win32_thread_ctx_t;

static win32_thread_ctx_t thread_ctxs[32];
static int thread_ctx_count = 0;

static void win32_thread_wrapper(void) {
    /* Find our context — search newest first to avoid stale entries
     * from reused handle slots */
    int tid = task_get_current();
    win32_thread_ctx_t *ctx = NULL;
    for (int i = thread_ctx_count - 1; i >= 0; i--) {
        if (thread_ctxs[i].handle == INVALID_HANDLE_VALUE)
            continue;  /* already consumed */
        win32_handle_t *wh = get_handle(thread_ctxs[i].handle);
        if (wh && wh->tid == tid) {
            ctx = &thread_ctxs[i];
            break;
        }
    }

    DWORD exit_code = 0;
    if (ctx && ctx->start) {
        exit_code = ctx->start(ctx->param);
    }

    /* Mark thread handle as completed and clear the ctx slot */
    if (ctx) {
        win32_handle_t *wh = get_handle(ctx->handle);
        if (wh) {
            wh->thread_exit = exit_code;
            wh->thread_done = 1;
        }
        ctx->handle = INVALID_HANDLE_VALUE;  /* mark slot as consumed */
    }

    task_exit();
}

HANDLE WINAPI shim_CreateThread(
    LPVOID lpThreadAttributes, DWORD dwStackSize,
    LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter,
    DWORD dwCreationFlags, LPDWORD lpThreadId)
{
    (void)lpThreadAttributes; (void)dwStackSize; (void)dwCreationFlags;

    HANDLE h = alloc_handle(HTYPE_THREAD);
    if (h == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

    win32_handle_t *wh = get_handle(h);

    /* Store thread context */
    if (thread_ctx_count >= 32) {
        free_handle(h);
        return INVALID_HANDLE_VALUE;
    }
    win32_thread_ctx_t *ctx = &thread_ctxs[thread_ctx_count++];
    ctx->start = lpStartAddress;
    ctx->param = lpParameter;
    ctx->handle = h;

    int tid = task_create_thread("win32", win32_thread_wrapper, 1);
    if (tid < 0) {
        thread_ctx_count--;
        free_handle(h);
        return INVALID_HANDLE_VALUE;
    }

    wh->tid = tid;
    wh->thread_done = 0;
    wh->thread_exit = 0;

    if (lpThreadId) *lpThreadId = (DWORD)tid;

    DBG("CreateThread: tid=%d handle=%u", tid, (unsigned)h);
    return h;
}

void WINAPI shim_ExitThread(DWORD dwExitCode) {
    int tid = task_get_current();
    /* Find our handle and set exit code */
    for (int i = 3; i < MAX_HANDLES; i++) {
        if (handle_table[i].type == HTYPE_THREAD && handle_table[i].tid == tid) {
            handle_table[i].thread_exit = dwExitCode;
            handle_table[i].thread_done = 1;
            break;
        }
    }
    task_exit();
}

static BOOL WINAPI shim_TerminateThread(HANDLE hThread, DWORD dwExitCode) {
    win32_handle_t *wh = get_handle(hThread);
    if (!wh || wh->type != HTYPE_THREAD) return FALSE;
    wh->thread_exit = dwExitCode;
    wh->thread_done = 1;
    /* Kill the task */
    task_info_t *t = task_get(wh->tid);
    if (t) t->killed = 1;
    return TRUE;
}

static BOOL WINAPI shim_GetExitCodeThread(HANDLE hThread, LPDWORD lpExitCode) {
    win32_handle_t *wh = get_handle(hThread);
    if (!wh || wh->type != HTYPE_THREAD) return FALSE;
    if (lpExitCode) {
        if (wh->thread_done)
            *lpExitCode = wh->thread_exit;
        else
            *lpExitCode = 259; /* STILL_ACTIVE */
    }
    return TRUE;
}

/* ── Thread Local Storage (TLS) ──────────────────────────────── */

#define TLS_MAX_SLOTS 64

/* TLS: per-slot, per-task storage */
static int     tls_slot_used[TLS_MAX_SLOTS];
static void   *tls_values[TASK_MAX][TLS_MAX_SLOTS];
static int     tls_initialized = 0;

static void tls_init(void) {
    if (tls_initialized) return;
    memset(tls_slot_used, 0, sizeof(tls_slot_used));
    memset(tls_values, 0, sizeof(tls_values));
    tls_initialized = 1;
}

static DWORD WINAPI shim_TlsAlloc(void) {
    tls_init();
    for (int i = 0; i < TLS_MAX_SLOTS; i++) {
        if (!tls_slot_used[i]) {
            tls_slot_used[i] = 1;
            /* Clear this slot for all tasks */
            for (int t = 0; t < TASK_MAX; t++)
                tls_values[t][i] = NULL;
            return (DWORD)i;
        }
    }
    last_error = 8; /* ERROR_NOT_ENOUGH_MEMORY */
    return 0xFFFFFFFF; /* TLS_OUT_OF_INDEXES */
}

static BOOL WINAPI shim_TlsFree(DWORD dwTlsIndex) {
    tls_init();
    if (dwTlsIndex >= TLS_MAX_SLOTS) return FALSE;
    tls_slot_used[dwTlsIndex] = 0;
    return TRUE;
}

static LPVOID WINAPI shim_TlsGetValue(DWORD dwTlsIndex) {
    tls_init();
    if (dwTlsIndex >= TLS_MAX_SLOTS) {
        last_error = 87; /* ERROR_INVALID_PARAMETER */
        return NULL;
    }
    int tid = task_get_current();
    if (tid < 0 || tid >= TASK_MAX) return NULL;
    last_error = 0;
    return tls_values[tid][dwTlsIndex];
}

static BOOL WINAPI shim_TlsSetValue(DWORD dwTlsIndex, LPVOID lpTlsValue) {
    tls_init();
    if (dwTlsIndex >= TLS_MAX_SLOTS) return FALSE;
    int tid = task_get_current();
    if (tid < 0 || tid >= TASK_MAX) return FALSE;
    tls_values[tid][dwTlsIndex] = lpTlsValue;
    return TRUE;
}

/* FLS (Fiber Local Storage) — map to TLS for now */
static DWORD WINAPI shim_FlsAlloc(void *callback) {
    (void)callback;
    return shim_TlsAlloc();
}
static BOOL WINAPI shim_FlsFree(DWORD idx) { return shim_TlsFree(idx); }
static LPVOID WINAPI shim_FlsGetValue(DWORD idx) { return shim_TlsGetValue(idx); }
static BOOL WINAPI shim_FlsSetValue(DWORD idx, LPVOID val) { return shim_TlsSetValue(idx, val); }

/* ── Critical Sections ───────────────────────────────────────── */

static void WINAPI shim_InitializeCriticalSection(CRITICAL_SECTION *cs) {
    if (!cs) return;
    cs->LockCount = -1;  /* unlocked */
    cs->RecursionCount = 0;
    cs->OwningThread = 0;
    cs->SpinCount = 0;
}

static BOOL WINAPI shim_InitializeCriticalSectionAndSpinCount(
    CRITICAL_SECTION *cs, DWORD dwSpinCount)
{
    shim_InitializeCriticalSection(cs);
    cs->SpinCount = dwSpinCount;
    return TRUE;
}

static void WINAPI shim_EnterCriticalSection(CRITICAL_SECTION *cs) {
    if (!cs) return;
    DWORD me = (DWORD)task_get_current();

    /* Fast path: already own it (recursive) */
    if (cs->OwningThread == me && cs->RecursionCount > 0) {
        cs->RecursionCount++;
        return;
    }

    /* Spin/yield until we acquire */
    while (1) {
        uint32_t flags = irq_save();
        if (cs->LockCount < 0 || cs->RecursionCount == 0) {
            /* Lock is free */
            cs->LockCount = 0;
            cs->RecursionCount = 1;
            cs->OwningThread = me;
            irq_restore(flags);
            return;
        }
        irq_restore(flags);
        task_yield();
    }
}

static BOOL WINAPI shim_TryEnterCriticalSection(CRITICAL_SECTION *cs) {
    if (!cs) return FALSE;
    DWORD me = (DWORD)task_get_current();

    if (cs->OwningThread == me && cs->RecursionCount > 0) {
        cs->RecursionCount++;
        return TRUE;
    }

    uint32_t flags = irq_save();
    if (cs->LockCount < 0 || cs->RecursionCount == 0) {
        cs->LockCount = 0;
        cs->RecursionCount = 1;
        cs->OwningThread = me;
        irq_restore(flags);
        return TRUE;
    }
    irq_restore(flags);
    return FALSE;
}

static void WINAPI shim_LeaveCriticalSection(CRITICAL_SECTION *cs) {
    if (!cs) return;
    cs->RecursionCount--;
    if (cs->RecursionCount == 0) {
        cs->OwningThread = 0;
        cs->LockCount = -1; /* unlocked */
    }
}

static void WINAPI shim_DeleteCriticalSection(CRITICAL_SECTION *cs) {
    if (!cs) return;
    cs->LockCount = -1;
    cs->RecursionCount = 0;
    cs->OwningThread = 0;
}

/* ── Events ──────────────────────────────────────────────────── */

static HANDLE WINAPI shim_CreateEventA(
    LPVOID lpEventAttributes, BOOL bManualReset,
    BOOL bInitialState, LPCSTR lpName)
{
    (void)lpEventAttributes; (void)lpName;

    HANDLE h = alloc_handle(HTYPE_EVENT);
    if (h == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

    win32_handle_t *wh = get_handle(h);
    wh->manual_reset = bManualReset;
    wh->signaled = bInitialState;
    return h;
}

static BOOL WINAPI shim_SetEvent(HANDLE hEvent) {
    win32_handle_t *wh = get_handle(hEvent);
    if (!wh || wh->type != HTYPE_EVENT) return FALSE;
    wh->signaled = 1;
    return TRUE;
}

static BOOL WINAPI shim_ResetEvent(HANDLE hEvent) {
    win32_handle_t *wh = get_handle(hEvent);
    if (!wh || wh->type != HTYPE_EVENT) return FALSE;
    wh->signaled = 0;
    return TRUE;
}

/* ── Mutexes ─────────────────────────────────────────────────── */

static HANDLE WINAPI shim_CreateMutexA(
    LPVOID lpMutexAttributes, BOOL bInitialOwner, LPCSTR lpName)
{
    (void)lpMutexAttributes; (void)lpName;

    HANDLE h = alloc_handle(HTYPE_MUTEX);
    if (h == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

    win32_handle_t *wh = get_handle(h);
    if (bInitialOwner) {
        wh->mutex_owner = (DWORD)task_get_current();
        wh->mutex_count = 1;
    }
    return h;
}

static BOOL WINAPI shim_ReleaseMutex(HANDLE hMutex) {
    win32_handle_t *wh = get_handle(hMutex);
    if (!wh || wh->type != HTYPE_MUTEX) return FALSE;
    DWORD me = (DWORD)task_get_current();
    if (wh->mutex_owner != me) return FALSE;
    wh->mutex_count--;
    if (wh->mutex_count == 0)
        wh->mutex_owner = 0;
    return TRUE;
}

/* ── Semaphores ──────────────────────────────────────────────── */

static HANDLE WINAPI shim_CreateSemaphoreA(
    LPVOID lpSemaphoreAttributes, LONG lInitialCount,
    LONG lMaximumCount, LPCSTR lpName)
{
    (void)lpSemaphoreAttributes; (void)lpName;

    HANDLE h = alloc_handle(HTYPE_SEMAPHORE);
    if (h == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

    win32_handle_t *wh = get_handle(h);
    wh->sem_count = lInitialCount;
    wh->sem_max = lMaximumCount;
    return h;
}

static BOOL WINAPI shim_ReleaseSemaphore(
    HANDLE hSemaphore, LONG lReleaseCount, LONG *lpPreviousCount)
{
    win32_handle_t *wh = get_handle(hSemaphore);
    if (!wh || wh->type != HTYPE_SEMAPHORE) return FALSE;

    uint32_t flags = irq_save();
    if (lpPreviousCount) *lpPreviousCount = wh->sem_count;
    wh->sem_count += lReleaseCount;
    if (wh->sem_count > wh->sem_max)
        wh->sem_count = wh->sem_max;
    irq_restore(flags);
    return TRUE;
}

/* ── WaitForSingleObject ─────────────────────────────────────── */

static DWORD WINAPI shim_WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds) {
    win32_handle_t *wh = get_handle(hHandle);
    if (!wh) return WAIT_FAILED;

    uint32_t start = pit_get_ticks();
    uint32_t timeout_ticks = (dwMilliseconds == INFINITE)
        ? 0xFFFFFFFF
        : (dwMilliseconds * 120 / 1000);

    while (1) {
        uint32_t flags;

        switch (wh->type) {
        case HTYPE_THREAD:
            if (wh->thread_done) return WAIT_OBJECT_0;
            break;

        case HTYPE_EVENT:
            flags = irq_save();
            if (wh->signaled) {
                if (!wh->manual_reset)
                    wh->signaled = 0; /* auto-reset */
                irq_restore(flags);
                return WAIT_OBJECT_0;
            }
            irq_restore(flags);
            break;

        case HTYPE_MUTEX: {
            DWORD me = (DWORD)task_get_current();
            flags = irq_save();
            if (wh->mutex_owner == 0) {
                wh->mutex_owner = me;
                wh->mutex_count = 1;
                irq_restore(flags);
                return WAIT_OBJECT_0;
            }
            if (wh->mutex_owner == me) {
                wh->mutex_count++;
                irq_restore(flags);
                return WAIT_OBJECT_0;
            }
            irq_restore(flags);
            break;
        }

        case HTYPE_SEMAPHORE:
            flags = irq_save();
            if (wh->sem_count > 0) {
                wh->sem_count--;
                irq_restore(flags);
                return WAIT_OBJECT_0;
            }
            irq_restore(flags);
            break;

        default:
            return WAIT_FAILED;
        }

        /* Check timeout */
        if (dwMilliseconds != INFINITE) {
            uint32_t elapsed = pit_get_ticks() - start;
            if (elapsed >= timeout_ticks)
                return WAIT_TIMEOUT;
        }

        task_yield();
    }
}

static DWORD WINAPI shim_WaitForMultipleObjects(
    DWORD nCount, const HANDLE *lpHandles, BOOL bWaitAll, DWORD dwMilliseconds)
{
    if (!lpHandles || nCount == 0) return WAIT_FAILED;

    uint32_t start = pit_get_ticks();
    uint32_t timeout_ticks = (dwMilliseconds == INFINITE)
        ? 0xFFFFFFFF
        : (dwMilliseconds * 120 / 1000);

    while (1) {
        if (bWaitAll) {
            /* Wait for ALL objects */
            int all_signaled = 1;
            for (DWORD i = 0; i < nCount; i++) {
                DWORD r = shim_WaitForSingleObject(lpHandles[i], 0);
                if (r != WAIT_OBJECT_0) {
                    all_signaled = 0;
                    break;
                }
            }
            if (all_signaled) return WAIT_OBJECT_0;
        } else {
            /* Wait for ANY object */
            for (DWORD i = 0; i < nCount; i++) {
                DWORD r = shim_WaitForSingleObject(lpHandles[i], 0);
                if (r == WAIT_OBJECT_0)
                    return WAIT_OBJECT_0 + i;
            }
        }

        if (dwMilliseconds != INFINITE) {
            uint32_t elapsed = pit_get_ticks() - start;
            if (elapsed >= timeout_ticks)
                return WAIT_TIMEOUT;
        }

        task_yield();
    }
}

/* ── Interlocked Operations ──────────────────────────────────── */

static LONG WINAPI shim_InterlockedIncrement(volatile LONG *addend) {
    uint32_t flags = irq_save();
    LONG result = ++(*addend);
    irq_restore(flags);
    return result;
}

static LONG WINAPI shim_InterlockedDecrement(volatile LONG *addend) {
    uint32_t flags = irq_save();
    LONG result = --(*addend);
    irq_restore(flags);
    return result;
}

static LONG WINAPI shim_InterlockedExchange(volatile LONG *target, LONG value) {
    uint32_t flags = irq_save();
    LONG old = *target;
    *target = value;
    irq_restore(flags);
    return old;
}

static LONG WINAPI shim_InterlockedCompareExchange(
    volatile LONG *dest, LONG exchange, LONG comparand)
{
    uint32_t flags = irq_save();
    LONG old = *dest;
    if (old == comparand)
        *dest = exchange;
    irq_restore(flags);
    return old;
}

static LONG WINAPI shim_InterlockedExchangeAdd(volatile LONG *addend, LONG value) {
    uint32_t flags = irq_save();
    LONG old = *addend;
    *addend += value;
    irq_restore(flags);
    return old;
}

/* ── Stubs ───────────────────────────────────────────────────── */

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

    /* Threading */
    { "CreateThread",           (void *)shim_CreateThread },
    { "ExitThread",             (void *)shim_ExitThread },
    { "TerminateThread",        (void *)shim_TerminateThread },
    { "GetExitCodeThread",      (void *)shim_GetExitCodeThread },

    /* Thread Local Storage */
    { "TlsAlloc",              (void *)shim_TlsAlloc },
    { "TlsFree",               (void *)shim_TlsFree },
    { "TlsGetValue",           (void *)shim_TlsGetValue },
    { "TlsSetValue",           (void *)shim_TlsSetValue },
    { "FlsAlloc",              (void *)shim_FlsAlloc },
    { "FlsFree",               (void *)shim_FlsFree },
    { "FlsGetValue",           (void *)shim_FlsGetValue },
    { "FlsSetValue",           (void *)shim_FlsSetValue },

    /* Critical Sections */
    { "InitializeCriticalSection",           (void *)shim_InitializeCriticalSection },
    { "InitializeCriticalSectionAndSpinCount", (void *)shim_InitializeCriticalSectionAndSpinCount },
    { "InitializeCriticalSectionEx",         (void *)shim_InitializeCriticalSectionAndSpinCount },
    { "EnterCriticalSection",                (void *)shim_EnterCriticalSection },
    { "TryEnterCriticalSection",             (void *)shim_TryEnterCriticalSection },
    { "LeaveCriticalSection",                (void *)shim_LeaveCriticalSection },
    { "DeleteCriticalSection",               (void *)shim_DeleteCriticalSection },

    /* Events */
    { "CreateEventA",          (void *)shim_CreateEventA },
    { "CreateEventW",          (void *)shim_CreateEventA },
    { "SetEvent",              (void *)shim_SetEvent },
    { "ResetEvent",            (void *)shim_ResetEvent },

    /* Mutexes */
    { "CreateMutexA",          (void *)shim_CreateMutexA },
    { "CreateMutexW",          (void *)shim_CreateMutexA },
    { "ReleaseMutex",          (void *)shim_ReleaseMutex },

    /* Semaphores */
    { "CreateSemaphoreA",      (void *)shim_CreateSemaphoreA },
    { "CreateSemaphoreW",      (void *)shim_CreateSemaphoreA },
    { "ReleaseSemaphore",      (void *)shim_ReleaseSemaphore },

    /* Wait functions */
    { "WaitForSingleObject",       (void *)shim_WaitForSingleObject },
    { "WaitForMultipleObjects",    (void *)shim_WaitForMultipleObjects },

    /* Interlocked */
    { "InterlockedIncrement",          (void *)shim_InterlockedIncrement },
    { "InterlockedDecrement",          (void *)shim_InterlockedDecrement },
    { "InterlockedExchange",           (void *)shim_InterlockedExchange },
    { "InterlockedCompareExchange",    (void *)shim_InterlockedCompareExchange },
    { "InterlockedExchangeAdd",        (void *)shim_InterlockedExchangeAdd },

    /* Stubs — commonly imported but not critical */
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
