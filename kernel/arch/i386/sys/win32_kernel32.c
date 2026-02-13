#include <kernel/win32_types.h>
#include <kernel/win32_seh.h>
#include <kernel/fs.h>
#include <kernel/task.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/io.h>
#include <kernel/pipe.h>
#include <kernel/pe_loader.h>
#include <kernel/env.h>
#include <kernel/config.h>
#include <kernel/user.h>
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

    /* Process state (HTYPE_PROCESS) */
    int             proc_tid;       /* main thread task slot */
    DWORD           proc_exit;      /* exit code */
    volatile int    proc_done;      /* 1 when process finished */
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

/* ── Directory Enumeration (FindFirstFile/FindNextFile) ───────── */

#define HTYPE_FINDFILE 11  /* extends handle_type_t */
#define MAX_PATH 260

/* Must match real Windows layout exactly (318 bytes) so mingw-compiled
 * PE code and our shim agree on field offsets */
typedef struct {
    DWORD    dwFileAttributes;
    DWORD    ftCreationTime[2];      /* FILETIME: 8 bytes */
    DWORD    ftLastAccessTime[2];    /* FILETIME: 8 bytes */
    DWORD    ftLastWriteTime[2];     /* FILETIME: 8 bytes */
    DWORD    nFileSizeHigh;
    DWORD    nFileSizeLow;
    DWORD    dwReserved0;
    DWORD    dwReserved1;
    char     cFileName[MAX_PATH];
    char     cAlternateFileName[14];
} WIN32_FIND_DATAA;

#define FILE_ATTRIBUTE_NORMAL    0x00000080
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_ARCHIVE   0x00000020
#define FILE_ATTRIBUTE_READONLY  0x00000001

static HANDLE WINAPI shim_FindFirstFileA(LPCSTR lpFileName, WIN32_FIND_DATAA *lpFindData) {
    (void)lpFileName;
    if (!lpFindData) return INVALID_HANDLE_VALUE;

    /* Use fs_enumerate_directory to get all entries */
    HANDLE h = alloc_handle((handle_type_t)HTYPE_FINDFILE);
    if (h == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

    win32_handle_t *wh = get_handle(h);

    /* Allocate buffer for directory listing */
    fs_dir_entry_info_t *entries = (fs_dir_entry_info_t *)malloc(
        sizeof(fs_dir_entry_info_t) * 64);
    if (!entries) { free_handle(h); return INVALID_HANDLE_VALUE; }

    int count = fs_enumerate_directory(entries, 64, 0);
    if (count <= 0) {
        free(entries);
        free_handle(h);
        last_error = 18;  /* ERROR_NO_MORE_FILES */
        return INVALID_HANDLE_VALUE;
    }

    wh->buffer = (uint8_t *)entries;
    wh->size = (size_t)count;
    wh->pos = 0;

    /* Fill first result */
    memset(lpFindData, 0, sizeof(WIN32_FIND_DATAA));
    strncpy(lpFindData->cFileName, entries[0].name, MAX_PATH - 1);
    lpFindData->nFileSizeLow = entries[0].size;
    lpFindData->dwFileAttributes = (entries[0].type == INODE_DIR)
        ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    wh->pos = 1;

    return h;
}

static BOOL WINAPI shim_FindNextFileA(HANDLE hFind, WIN32_FIND_DATAA *lpFindData) {
    win32_handle_t *wh = get_handle(hFind);
    if (!wh || wh->type != (handle_type_t)HTYPE_FINDFILE || !lpFindData)
        return FALSE;

    if (wh->pos >= wh->size) {
        last_error = 18;  /* ERROR_NO_MORE_FILES */
        return FALSE;
    }

    fs_dir_entry_info_t *entries = (fs_dir_entry_info_t *)wh->buffer;
    memset(lpFindData, 0, sizeof(WIN32_FIND_DATAA));
    strncpy(lpFindData->cFileName, entries[wh->pos].name, MAX_PATH - 1);
    lpFindData->nFileSizeLow = entries[wh->pos].size;
    lpFindData->dwFileAttributes = (entries[wh->pos].type == INODE_DIR)
        ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    wh->pos++;

    return TRUE;
}

static BOOL WINAPI shim_FindClose(HANDLE hFind) {
    return shim_CloseHandle(hFind);
}

/* ── File Attributes & Info ──────────────────────────────────── */

/* Check if a path exists as a file (non-stdcall, safe to call internally).
 * Uses a static buffer so fs_read_file overflow won't corrupt the stack. */
static int file_exists(const char *path) {
    static uint8_t exist_buf[128];  /* static — overflow-safe */
    size_t sz = 1;
    return fs_read_file(path, exist_buf, &sz) >= 0;
}

static DWORD WINAPI shim_GetFileAttributesA(LPCSTR lpFileName) {
    /* Try reading as file — use static buffer helper */
    if (file_exists(lpFileName))
        return FILE_ATTRIBUTE_NORMAL;

    /* Try as directory — save/restore cwd */
    uint32_t saved = fs_get_cwd_inode();
    if (fs_change_directory(lpFileName) >= 0) {
        fs_change_directory_by_inode(saved);
        return FILE_ATTRIBUTE_DIRECTORY;
    }
    fs_change_directory_by_inode(saved);

    last_error = 2;  /* ERROR_FILE_NOT_FOUND */
    return (DWORD)-1;  /* INVALID_FILE_ATTRIBUTES */
}

static DWORD WINAPI shim_GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh) {
    win32_handle_t *wh = get_handle(hFile);
    if (!wh || wh->type != HTYPE_FILE) return (DWORD)-1;
    if (lpFileSizeHigh) *lpFileSizeHigh = 0;
    return (DWORD)wh->size;
}

static DWORD WINAPI shim_GetFileType(HANDLE hFile) {
    win32_handle_t *wh = get_handle(hFile);
    if (!wh) return 0;  /* FILE_TYPE_UNKNOWN */
    if (wh->type == HTYPE_CONSOLE) return 2;  /* FILE_TYPE_CHAR */
    if (wh->type == HTYPE_FILE) return 1;     /* FILE_TYPE_DISK */
    return 0;
}

/* ── File Pointer (random access) ────────────────────────────── */

#define FILE_BEGIN   0
#define FILE_CURRENT 1
#define FILE_END     2

static DWORD WINAPI shim_SetFilePointer(
    HANDLE hFile, LONG lDistanceToMove, LONG *lpDistHigh, DWORD dwMoveMethod)
{
    (void)lpDistHigh;
    win32_handle_t *wh = get_handle(hFile);
    if (!wh || wh->type != HTYPE_FILE) return (DWORD)-1;

    long new_pos;
    switch (dwMoveMethod) {
        case FILE_BEGIN:   new_pos = lDistanceToMove; break;
        case FILE_CURRENT: new_pos = (long)wh->pos + lDistanceToMove; break;
        case FILE_END:     new_pos = (long)wh->size + lDistanceToMove; break;
        default: return (DWORD)-1;
    }
    if (new_pos < 0) new_pos = 0;
    if ((size_t)new_pos > wh->size) wh->size = (size_t)new_pos;
    wh->pos = (size_t)new_pos;
    return (DWORD)wh->pos;
}

static BOOL WINAPI shim_SetEndOfFile(HANDLE hFile) {
    win32_handle_t *wh = get_handle(hFile);
    if (!wh || wh->type != HTYPE_FILE) return FALSE;
    wh->size = wh->pos;
    return TRUE;
}

/* ── Directory Operations ────────────────────────────────────── */

static BOOL WINAPI shim_CreateDirectoryA(LPCSTR lpPathName, LPVOID lpSecAttr) {
    (void)lpSecAttr;
    return fs_create_file(lpPathName, 1) >= 0 ? TRUE : FALSE;
}

static BOOL WINAPI shim_RemoveDirectoryA(LPCSTR lpPathName) {
    return fs_delete_file(lpPathName) >= 0 ? TRUE : FALSE;
}

/* ── Temp Files ──────────────────────────────────────────────── */

static DWORD WINAPI shim_GetTempPathA(DWORD nBufferLength, LPSTR lpBuffer) {
    const char *tmp = "/tmp";
    size_t len = 4;
    if (lpBuffer && nBufferLength > len) {
        memcpy(lpBuffer, tmp, len);
        lpBuffer[len] = '\0';
    }
    return (DWORD)len;
}

static UINT WINAPI shim_GetTempFileNameA(
    LPCSTR lpPathName, LPCSTR lpPrefixString, UINT uUnique, LPSTR lpTempFileName)
{
    (void)lpPathName; (void)uUnique;
    /* Generate a simple temp filename */
    static int temp_counter = 0;
    char prefix[4] = "tmp";
    if (lpPrefixString) {
        prefix[0] = lpPrefixString[0] ? lpPrefixString[0] : 't';
        prefix[1] = lpPrefixString[1] ? lpPrefixString[1] : 'm';
        prefix[2] = lpPrefixString[2] ? lpPrefixString[2] : 'p';
    }
    snprintf(lpTempFileName, MAX_PATH, "/tmp/%c%c%c%04X.tmp",
             prefix[0], prefix[1], prefix[2], ++temp_counter);
    return temp_counter;
}

/* ── File Operations (Delete/Move/Copy) ──────────────────────── */

static BOOL WINAPI shim_DeleteFileA(LPCSTR lpFileName) {
    return fs_delete_file(lpFileName) >= 0 ? TRUE : FALSE;
}

static BOOL WINAPI shim_MoveFileA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName) {
    return fs_rename(lpExistingFileName, lpNewFileName) >= 0 ? TRUE : FALSE;
}

static BOOL WINAPI shim_CopyFileA(LPCSTR lpSrc, LPCSTR lpDst, BOOL bFailIfExists) {
    uint8_t *buf = malloc(MAX_FILE_SIZE);
    if (!buf) return FALSE;

    size_t sz = MAX_FILE_SIZE;
    if (fs_read_file(lpSrc, buf, &sz) < 0) {
        free(buf);
        return FALSE;
    }

    if (bFailIfExists) {
        size_t check = 1;
        uint8_t tmp;
        if (fs_read_file(lpDst, &tmp, &check) >= 0) {
            free(buf);
            last_error = 80;  /* ERROR_FILE_EXISTS */
            return FALSE;
        }
    }

    fs_create_file(lpDst, 0);
    int ret = fs_write_file(lpDst, buf, sz);
    free(buf);
    return ret >= 0 ? TRUE : FALSE;
}

/* ── Module / Path Queries ───────────────────────────────────── */

static DWORD WINAPI shim_GetModuleFileNameA(HMODULE hModule, LPSTR lpFilename, DWORD nSize) {
    (void)hModule;
    const char *name = "C:\\app.exe";
    size_t len = strlen(name);
    if (len >= nSize) len = nSize - 1;
    memcpy(lpFilename, name, len);
    lpFilename[len] = '\0';
    return (DWORD)len;
}

static DWORD WINAPI shim_GetCurrentDirectoryA(DWORD nBufferLength, LPSTR lpBuffer) {
    const char *cwd = fs_get_cwd();
    size_t len = strlen(cwd);
    if (lpBuffer && nBufferLength > len) {
        memcpy(lpBuffer, cwd, len);
        lpBuffer[len] = '\0';
    }
    return (DWORD)len;
}

static BOOL WINAPI shim_SetCurrentDirectoryA(LPCSTR lpPathName) {
    return fs_change_directory(lpPathName) >= 0 ? TRUE : FALSE;
}

static DWORD WINAPI shim_GetFullPathNameA(
    LPCSTR lpFileName, DWORD nBufferLength, LPSTR lpBuffer, LPSTR *lpFilePart)
{
    /* Simple: prepend cwd if path doesn't start with / */
    const char *cwd = fs_get_cwd();
    size_t len;

    if (lpFileName[0] == '/' || lpFileName[0] == '\\') {
        len = strlen(lpFileName);
        if (lpBuffer && nBufferLength > len) {
            memcpy(lpBuffer, lpFileName, len);
            lpBuffer[len] = '\0';
        }
    } else {
        size_t cwd_len = strlen(cwd);
        size_t name_len = strlen(lpFileName);
        len = cwd_len + 1 + name_len;
        if (lpBuffer && nBufferLength > len) {
            memcpy(lpBuffer, cwd, cwd_len);
            lpBuffer[cwd_len] = '/';
            memcpy(lpBuffer + cwd_len + 1, lpFileName, name_len);
            lpBuffer[len] = '\0';
        }
    }

    if (lpFilePart && lpBuffer) {
        char *last_sep = strrchr(lpBuffer, '/');
        *lpFilePart = last_sep ? last_sep + 1 : lpBuffer;
    }
    return (DWORD)len;
}

static DWORD WINAPI shim_GetLongPathNameA(LPCSTR lpszShort, LPSTR lpszLong, DWORD cchBuffer) {
    /* Our FS doesn't have short names — just copy */
    size_t len = strlen(lpszShort);
    if (lpszLong && cchBuffer > len) {
        memcpy(lpszLong, lpszShort, len);
        lpszLong[len] = '\0';
    }
    return (DWORD)len;
}

/* ── Overlapped I/O stubs ────────────────────────────────────── */
/* Chromium may reference these but we run everything synchronously */

static BOOL WINAPI shim_GetOverlappedResult(
    HANDLE hFile, LPVOID lpOverlapped, LPDWORD lpBytes, BOOL bWait)
{
    (void)hFile; (void)lpOverlapped; (void)bWait;
    if (lpBytes) *lpBytes = 0;
    return TRUE;
}

static BOOL WINAPI shim_CancelIo(HANDLE hFile) {
    (void)hFile;
    return TRUE;
}

/* ── Child process context (needed by ExitProcess + CreateProcessA) ──── */

#define MAX_CHILD_PROCS 16
#define PE_VIRTUAL_BASE 0x10000000   /* per-process PE image base */
#define PE_MAX_FRAMES   32           /* max 128KB per PE image */

typedef struct {
    int      in_use;
    char     exe_path[64];
    HANDLE   proc_handle;
    HANDLE   thread_handle;
    volatile int ready;   /* set to 1 after tid is assigned */
    int      tid;
    /* Per-process address space */
    uint32_t page_dir;              /* child's page directory (phys) */
    uint32_t phys_frames[PE_MAX_FRAMES]; /* PMM frames for PE image */
    int      num_frames;
    uint32_t page_table;            /* page table for PDE 64 (phys) */
} child_ctx_t;

static child_ctx_t child_ctxs[MAX_CHILD_PROCS];

/* ── Per-process cleanup helper ──────────────────────────────── */

static void pe_child_cleanup(child_ctx_t *ctx) {
    if (!ctx->page_dir) return;

    int tid = task_get_current();
    DBG("pe_child_cleanup: tid=%d PD=0x%x frames=%d PT=0x%x",
        tid, ctx->page_dir, ctx->num_frames, ctx->page_table);

    /* Switch back to kernel page directory before freeing pages */
    uint32_t kernel_pd = vmm_get_kernel_pagedir();
    __asm__ volatile ("mov %0, %%cr3" : : "r"(kernel_pd) : "memory");
    DBG("pe_child_cleanup: switched CR3 back to kernel PD=0x%x", kernel_pd);

    /* Update current task's page_dir */
    task_info_t *t = task_get(tid);
    if (t) {
        t->page_dir = kernel_pd;
        t->user_page_table = 0;
    }

    /* Free PE image frames */
    for (int i = 0; i < ctx->num_frames; i++) {
        if (ctx->phys_frames[i])
            pmm_free_frame(ctx->phys_frames[i]);
    }
    DBG("pe_child_cleanup: freed %d PMM frames", ctx->num_frames);

    /* Free page table for PDE 64 */
    if (ctx->page_table)
        pmm_free_frame(ctx->page_table);

    /* Free the page directory itself */
    vmm_destroy_user_pagedir(ctx->page_dir);
    DBG("pe_child_cleanup: freed PT=0x%x + PD=0x%x — cleanup complete",
        ctx->page_table, ctx->page_dir);

    ctx->page_dir = 0;
    ctx->page_table = 0;
    ctx->num_frames = 0;
}

/* ── Process / Module ────────────────────────────────────────── */

static void WINAPI shim_ExitProcess(UINT uExitCode) {
    DBG("ExitProcess(%u)", uExitCode);

    /* If this is a child process, clean up its private address space
     * and mark handles as done BEFORE calling task_exit(), otherwise
     * the parent's WaitForSingleObject will spin forever. */
    int tid = task_get_current();
    for (int i = 0; i < MAX_CHILD_PROCS; i++) {
        if (child_ctxs[i].in_use && child_ctxs[i].tid == tid) {
            DBG("ExitProcess: child slot %d, tid=%d, PD=0x%x — cleaning up",
                i, tid, child_ctxs[i].page_dir);
            /* Clean up per-process address space first (switches CR3 back) */
            pe_child_cleanup(&child_ctxs[i]);

            win32_handle_t *pwh = get_handle(child_ctxs[i].proc_handle);
            if (pwh && pwh->type == HTYPE_PROCESS) {
                pwh->proc_exit = uExitCode;
                pwh->proc_done = 1;
            }
            win32_handle_t *twh = get_handle(child_ctxs[i].thread_handle);
            if (twh && twh->type == HTYPE_THREAD) {
                twh->thread_exit = uExitCode;
                twh->thread_done = 1;
            }
            child_ctxs[i].in_use = 0;
            break;
        }
    }

    task_exit();
}

static HMODULE WINAPI shim_GetModuleHandleA(LPCSTR lpModuleName) {
    (void)lpModuleName;
    return (HMODULE)0x00400000;
}

/* ═══════════════════════════════════════════════════════════════
 *  DLL Loading — LoadLibraryA / FreeLibrary / GetProcAddress
 * ═══════════════════════════════════════════════════════════════ */

#define MAX_LOADED_DLLS 32
#define DLL_PROCESS_DETACH 0
#define DLL_PROCESS_ATTACH 1

/* Pseudo-handle base for shim-only DLLs (no real image on disk).
 * Must be outside identity-mapped range (0-256MB) to avoid collisions
 * with real DLL image bases. */
#define SHIM_HMODULE_BASE 0x5A000000

typedef struct {
    int      in_use;
    char     name[64];           /* "kernel32.dll" (lowercase) */
    uint32_t image_base;         /* where loaded in memory (0 = shim-only) */
    uint32_t image_size;
    int      refcount;
    /* Export directory (parsed at load time, only for real DLLs) */
    uint32_t num_functions;
    uint32_t num_names;
    uint32_t ordinal_base;
    uint32_t *addr_table;        /* absolute ptrs to AddressOfFunctions */
    uint32_t *name_table;        /* absolute ptrs to AddressOfNames */
    uint16_t *ordinal_table;     /* absolute ptrs to AddressOfNameOrdinals */
} loaded_dll_t;

static loaded_dll_t loaded_dlls[MAX_LOADED_DLLS];

/* Normalize a DLL name to lowercase */
static void dll_name_normalize(const char *src, char *dst, size_t dst_size) {
    size_t i = 0;
    for (; src[i] && i < dst_size - 1; i++) {
        char c = src[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        /* Convert backslash path separators */
        if (c == '\\') c = '/';
        dst[i] = c;
    }
    dst[i] = '\0';

    /* Strip leading path components — only keep the filename */
    char *slash = strrchr(dst, '/');
    if (slash) {
        size_t len = strlen(slash + 1);
        memmove(dst, slash + 1, len + 1);
    }
}

/* Find a loaded DLL by normalized name */
static loaded_dll_t *dll_find(const char *name_lower) {
    for (int i = 0; i < MAX_LOADED_DLLS; i++) {
        if (loaded_dlls[i].in_use && strcmp(loaded_dlls[i].name, name_lower) == 0)
            return &loaded_dlls[i];
    }
    return NULL;
}

/* Find a loaded DLL by HMODULE */
static loaded_dll_t *dll_find_by_handle(HMODULE hmod) {
    for (int i = 0; i < MAX_LOADED_DLLS; i++) {
        if (!loaded_dlls[i].in_use) continue;
        /* Real DLL: image_base matches */
        if (loaded_dlls[i].image_base && loaded_dlls[i].image_base == hmod)
            return &loaded_dlls[i];
        /* Shim-only DLL: synthetic handle = SHIM_HMODULE_BASE + slot */
        if (!loaded_dlls[i].image_base &&
            hmod == (HMODULE)(SHIM_HMODULE_BASE + i))
            return &loaded_dlls[i];
    }
    return NULL;
}

/* Parse PE export directory into a loaded_dll_t */
static void pe_parse_exports(loaded_dll_t *dll, pe_loaded_image_t *img) {
    if (img->export_dir_rva == 0 || img->export_dir_size == 0) {
        dll->num_functions = 0;
        dll->num_names = 0;
        return;
    }

    pe_export_directory_t *exp = (pe_export_directory_t *)(
        img->image_base + img->export_dir_rva);

    dll->num_functions = exp->num_functions;
    dll->num_names = exp->num_names;
    dll->ordinal_base = exp->ordinal_base;

    /* Point directly into the loaded image's tables */
    dll->addr_table = (uint32_t *)(img->image_base + exp->addr_table_rva);
    dll->name_table = (uint32_t *)(img->image_base + exp->name_table_rva);
    dll->ordinal_table = (uint16_t *)(img->image_base + exp->ordinal_table_rva);

    DBG("pe_parse_exports: '%s' — %u functions, %u names, ordinal_base=%u",
        dll->name, dll->num_functions, dll->num_names, dll->ordinal_base);
}

/* Resolve an export by name from a real DLL (binary search on name table) */
static void *dll_resolve_export(loaded_dll_t *dll, const char *func_name) {
    if (!dll->image_base || dll->num_names == 0)
        return NULL;

    /* Binary search through the name pointer table (sorted alphabetically) */
    int lo = 0, hi = (int)dll->num_names - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const char *name = (const char *)(dll->image_base + dll->name_table[mid]);
        int cmp = strcmp(func_name, name);
        if (cmp == 0) {
            /* Found — use the ordinal table to index into the address table */
            uint16_t ordinal = dll->ordinal_table[mid];
            uint32_t func_rva = dll->addr_table[ordinal];

            /* Check for forwarded export (RVA points inside export dir) */
            uint32_t exp_start = dll->addr_table[0]; /* approximate */
            (void)exp_start;
            /* For now, return the absolute address */
            return (void *)(dll->image_base + func_rva);
        } else if (cmp < 0) {
            hi = mid - 1;
        } else {
            lo = mid + 1;
        }
    }
    return NULL;
}

/* Check if a DLL name matches one of our shim tables */
static const win32_dll_shim_t *dll_find_shim(const char *name_lower) {
    /* Access the shim table from pe_loader.c via win32_resolve_import.
     * But we also need to check direct DLL name match. */
    extern const win32_dll_shim_t win32_ucrtbase;

    static const win32_dll_shim_t *all_shims[] = {
        &win32_kernel32, &win32_user32, &win32_gdi32,
        &win32_msvcrt, &win32_ucrtbase, &win32_advapi32, &win32_ws2_32,
        &win32_gdiplus, &win32_ole32, &win32_shell32,
        &win32_bcrypt, &win32_crypt32, NULL
    };

    for (int i = 0; all_shims[i]; i++) {
        /* Case-insensitive compare against shim dll_name */
        const char *a = all_shims[i]->dll_name;
        const char *b = name_lower;
        int match = 1;
        while (*a && *b) {
            char ca = *a, cb = *b;
            if (ca >= 'A' && ca <= 'Z') ca += 32;
            if (cb >= 'A' && cb <= 'Z') cb += 32;
            if (ca != cb) { match = 0; break; }
            a++; b++;
        }
        if (match && *a == *b)
            return all_shims[i];
    }

    /* Also match api-ms-win-crt-* DLLs to ucrtbase */
    if (strncmp(name_lower, "api-ms-win-crt-", 15) == 0)
        return &win32_ucrtbase;

    return NULL;
}

/* ── LoadLibraryA ────────────────────────────────────────────── */
static HMODULE WINAPI shim_LoadLibraryA(LPCSTR lpLibFileName) {
    if (!lpLibFileName || !lpLibFileName[0]) {
        last_error = 87;  /* ERROR_INVALID_PARAMETER */
        return (HMODULE)0;
    }

    /* Normalize name */
    char name_lower[64];
    dll_name_normalize(lpLibFileName, name_lower, sizeof(name_lower));

    DBG("LoadLibraryA: '%s' → '%s'", lpLibFileName, name_lower);

    /* Already loaded? Bump refcount */
    loaded_dll_t *existing = dll_find(name_lower);
    if (existing) {
        existing->refcount++;
        HMODULE h = existing->image_base
            ? (HMODULE)existing->image_base
            : (HMODULE)(SHIM_HMODULE_BASE + (existing - loaded_dlls));
        DBG("LoadLibraryA: '%s' already loaded, refcount=%d → 0x%x",
            name_lower, existing->refcount, h);
        return h;
    }

    /* Find a free slot */
    int slot = -1;
    for (int i = 0; i < MAX_LOADED_DLLS; i++) {
        if (!loaded_dlls[i].in_use) { slot = i; break; }
    }
    if (slot < 0) {
        last_error = 8;  /* ERROR_NOT_ENOUGH_MEMORY */
        return (HMODULE)0;
    }

    loaded_dll_t *dll = &loaded_dlls[slot];
    memset(dll, 0, sizeof(*dll));
    strncpy(dll->name, name_lower, sizeof(dll->name) - 1);
    dll->refcount = 1;
    dll->in_use = 1;

    /* Try to load from disk: /apps/{name}, then /{name} */
    char path[128];
    pe_loaded_image_t img;
    int loaded_from_disk = 0;

    snprintf(path, sizeof(path), "/apps/%s", name_lower);
    if (pe_load(path, &img) == 0) {
        loaded_from_disk = 1;
    } else {
        snprintf(path, sizeof(path), "/%s", name_lower);
        if (pe_load(path, &img) == 0) {
            loaded_from_disk = 1;
        }
    }

    if (loaded_from_disk) {
        /* Apply relocations and resolve imports */
        pe_apply_relocations(&img);
        pe_resolve_imports(&img);

        dll->image_base = img.image_base;
        dll->image_size = img.image_size;

        /* Parse export directory */
        pe_parse_exports(dll, &img);

        /* Call DllMain(DLL_PROCESS_ATTACH) if entry point exists */
        if (img.entry_point && img.entry_point != img.image_base) {
            typedef BOOL (__attribute__((stdcall)) *dll_main_t)(
                HMODULE hModule, DWORD ul_reason, LPVOID lpReserved);
            dll_main_t dll_main = (dll_main_t)img.entry_point;
            DBG("LoadLibraryA: calling DllMain(ATTACH) at 0x%x", img.entry_point);
            dll_main((HMODULE)img.image_base, DLL_PROCESS_ATTACH, NULL);
        }

        DBG("LoadLibraryA: loaded '%s' from disk at 0x%x, size=0x%x",
            name_lower, dll->image_base, dll->image_size);
        return (HMODULE)dll->image_base;
    }

    /* Not on disk — check if it matches a shim table */
    const win32_dll_shim_t *shim = dll_find_shim(name_lower);
    if (shim) {
        /* Shim-only DLL: image_base stays 0 */
        HMODULE h = (HMODULE)(SHIM_HMODULE_BASE + slot);
        DBG("LoadLibraryA: '%s' → shim-only handle 0x%x", name_lower, h);
        return h;
    }

    /* Completely unknown DLL — still return a shim handle so callers
     * that check for NULL don't crash; GetProcAddress will fail. */
    HMODULE h = (HMODULE)(SHIM_HMODULE_BASE + slot);
    DBG("LoadLibraryA: '%s' unknown, returning stub handle 0x%x", name_lower, h);
    return h;
}

/* ── LoadLibraryExA ──────────────────────────────────────────── */
static HMODULE WINAPI shim_LoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags) {
    (void)hFile; (void)dwFlags;
    return shim_LoadLibraryA(lpLibFileName);
}

/* ── FreeLibrary ─────────────────────────────────────────────── */
static BOOL WINAPI shim_FreeLibrary(HMODULE hLibModule) {
    loaded_dll_t *dll = dll_find_by_handle(hLibModule);
    if (!dll) return FALSE;

    dll->refcount--;
    DBG("FreeLibrary: '%s' refcount → %d", dll->name, dll->refcount);

    if (dll->refcount <= 0) {
        /* Call DllMain(DLL_PROCESS_DETACH) for real DLLs */
        if (dll->image_base) {
            /* We don't easily have the entry_point stored, so skip DllMain
             * on unload for now — most DLLs handle this gracefully. */
            /* Free the loaded image memory */
            pe_loaded_image_t tmp;
            memset(&tmp, 0, sizeof(tmp));
            tmp.image_base = dll->image_base;
            tmp.image_size = dll->image_size;
            pe_unload(&tmp);
        }
        memset(dll, 0, sizeof(*dll));
    }

    return TRUE;
}

/* ── GetProcAddress (rewritten for DLL loading) ──────────────── */
static void *WINAPI shim_GetProcAddress(HMODULE hModule, LPCSTR lpProcName) {
    if (!lpProcName) return NULL;

    /* If hModule matches a loaded DLL, try its exports first */
    loaded_dll_t *dll = dll_find_by_handle(hModule);
    if (dll) {
        /* Real DLL with export table — search PE exports */
        if (dll->image_base) {
            void *p = dll_resolve_export(dll, lpProcName);
            if (p) return p;
        }

        /* Shim-only or export not found — try shim tables by DLL name */
        void *p = win32_resolve_import(dll->name, lpProcName);
        if (p) return p;
    }

    /* Fallback: search all shim tables (covers GetModuleHandle(NULL) cases
     * and handles where hModule is the app's own base address) */
    void *p;
    p = win32_resolve_import("kernel32.dll", lpProcName);
    if (p) return p;
    p = win32_resolve_import("user32.dll", lpProcName);
    if (p) return p;
    p = win32_resolve_import("gdi32.dll", lpProcName);
    if (p) return p;
    p = win32_resolve_import("msvcrt.dll", lpProcName);
    if (p) return p;

    DBG("GetProcAddress: '%s' not found (hModule=0x%x)", lpProcName, hModule);
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

/* ── Process Creation ────────────────────────────────────────── */

/* Win32 STARTUPINFOA — must match real Windows layout */
typedef struct {
    DWORD  cb;
    LPSTR  lpReserved;
    LPSTR  lpDesktop;
    LPSTR  lpTitle;
    DWORD  dwX, dwY, dwXSize, dwYSize;
    DWORD  dwXCountChars, dwYCountChars;
    DWORD  dwFillAttribute;
    DWORD  dwFlags;
    WORD   wShowWindow;
    WORD   cbReserved2;
    LPBYTE lpReserved2;
    HANDLE hStdInput;
    HANDLE hStdOutput;
    HANDLE hStdError;
} STARTUPINFOA;

typedef struct {
    HANDLE hProcess;
    HANDLE hThread;
    DWORD  dwProcessId;
    DWORD  dwThreadId;
} PROCESS_INFORMATION;

static void child_process_wrapper(void) {
    int tid = task_get_current();
    DBG("child_process_wrapper: started, tid=%d", tid);

    /* Spin until parent sets ready flag — parent sets in_use=1 before
     * spawning us, then sets tid + ready=1 after task_create_thread returns */
    child_ctx_t *ctx = NULL;
    int attempts = 0;
    for (int attempt = 0; attempt < 100000 && !ctx; attempt++) {
        for (int i = MAX_CHILD_PROCS - 1; i >= 0; i--) {
            if (child_ctxs[i].in_use && child_ctxs[i].ready &&
                child_ctxs[i].tid == tid) {
                ctx = &child_ctxs[i];
                break;
            }
        }
        if (!ctx) { attempts++; task_yield(); }
    }

    if (!ctx || !ctx->exe_path[0]) {
        DBG("child_process_wrapper: no context for tid %d after %d attempts (ctx=%p)", tid, attempts, (void*)ctx);
        if (ctx) {
            DBG("  ctx->in_use=%d ready=%d tid=%d exe_path[0]=0x%x",
                ctx->in_use, ctx->ready, ctx->tid, (unsigned char)ctx->exe_path[0]);
        }
        for (int d = 0; d < MAX_CHILD_PROCS; d++) {
            if (child_ctxs[d].in_use || child_ctxs[d].tid) {
                DBG("  slot[%d]: in_use=%d ready=%d tid=%d exe='%s'",
                    d, child_ctxs[d].in_use, child_ctxs[d].ready,
                    child_ctxs[d].tid, child_ctxs[d].exe_path);
            }
        }
        task_exit();
        return;
    }

    DBG("child_process_wrapper: found ctx for tid %d, exe='%s' (after %d yields)", tid, ctx->exe_path, attempts);

    /* ── Stage 1: Load PE into identity-mapped staging area ──── */
    pe_loaded_image_t child_img;
    memset(&child_img, 0, sizeof(child_img));
    int ret = pe_load(ctx->exe_path, &child_img);
    DBG("child_process_wrapper: pe_load('%s') = %d, staging at 0x%x size=0x%x",
        ctx->exe_path, ret, child_img.image_base, child_img.image_size);

    /* ── Stage 2: Set virtual_base and apply relocations + imports ── */
    if (ret >= 0) {
        child_img.virtual_base = PE_VIRTUAL_BASE;
        ret = pe_apply_relocations(&child_img);
    }
    if (ret >= 0) ret = pe_resolve_imports(&child_img);

    if (ret < 0) {
        DBG("child_process_wrapper: pe_load/reloc/import failed for '%s' (%d)",
            ctx->exe_path, ret);
        goto done;
    }

    /* ── Stage 3: Allocate frames and copy staged image ──────── */
    {
        uint32_t pages = (child_img.image_size + PAGE_SIZE - 1) / PAGE_SIZE;
        if (pages > PE_MAX_FRAMES) {
            DBG("child_process_wrapper: image too large (%u pages > %d)",
                pages, PE_MAX_FRAMES);
            goto done;
        }

        ctx->num_frames = 0;
        for (uint32_t i = 0; i < pages; i++) {
            uint32_t frame = pmm_alloc_frame();
            if (!frame) {
                DBG("child_process_wrapper: PMM alloc failed at page %u", i);
                goto done;
            }
            /* Copy one page from staging area to frame */
            uint32_t src = child_img.image_base + i * PAGE_SIZE;
            memcpy((void *)frame, (void *)src, PAGE_SIZE);
            ctx->phys_frames[ctx->num_frames++] = frame;
        }

        DBG("child_process_wrapper: copied %d pages to PMM frames", ctx->num_frames);

        /* ── Stage 4: Map frames at PE_VIRTUAL_BASE in child PD ── */
        uint32_t pt_phys = 0;
        for (int i = 0; i < ctx->num_frames; i++) {
            uint32_t virt = PE_VIRTUAL_BASE + i * PAGE_SIZE;
            uint32_t ret_pt = vmm_map_user_page(ctx->page_dir, virt,
                ctx->phys_frames[i],
                PTE_PRESENT | PTE_WRITABLE | PTE_USER);
            if (!ret_pt) {
                DBG("child_process_wrapper: vmm_map_user_page failed at 0x%x", virt);
                goto done;
            }
            if (!pt_phys) pt_phys = ret_pt;
        }
        ctx->page_table = pt_phys;

        /* ── Stage 5: Switch to child page directory ───────────── */
        task_info_t *t = task_get(tid);
        if (t) {
            t->page_dir = ctx->page_dir;
            t->user_page_table = ctx->page_table;
        }
        __asm__ volatile ("mov %0, %%cr3" : : "r"(ctx->page_dir) : "memory");

        DBG("child_process_wrapper: switched to child PD=0x%x, calling entry at 0x%x",
            ctx->page_dir,
            PE_VIRTUAL_BASE + (child_img.entry_point - child_img.image_base));

        /* ── Stage 6: Call entry point at virtual address ──────── */
        uint32_t entry_va = PE_VIRTUAL_BASE +
            (child_img.entry_point - child_img.image_base);
        void (*entry)(void) = (void (*)(void))entry_va;
        entry();
        DBG("child_process_wrapper: entry returned");

        /* Switch back to kernel PD before cleanup */
        pe_child_cleanup(ctx);
    }

done:
    /* Mark process handle as done */
    {
        win32_handle_t *wh = get_handle(ctx->proc_handle);
        if (wh && wh->type == HTYPE_PROCESS) {
            wh->proc_exit = 0;
            wh->proc_done = 1;
        }
    }

    /* Mark thread handle as done */
    {
        win32_handle_t *th = get_handle(ctx->thread_handle);
        if (th && th->type == HTYPE_THREAD) {
            th->thread_exit = 0;
            th->thread_done = 1;
        }
    }

    ctx->in_use = 0;
    task_exit();
}

static BOOL WINAPI shim_CreateProcessA(
    LPCSTR lpApplicationName, LPSTR lpCommandLine,
    LPVOID lpProcessAttributes, LPVOID lpThreadAttributes,
    BOOL bInheritHandles, DWORD dwCreationFlags,
    LPVOID lpEnvironment, LPCSTR lpCurrentDirectory,
    STARTUPINFOA *lpStartupInfo, PROCESS_INFORMATION *lpProcessInformation)
{
    (void)lpProcessAttributes; (void)lpThreadAttributes;
    (void)bInheritHandles; (void)dwCreationFlags;
    (void)lpEnvironment; (void)lpCurrentDirectory;
    (void)lpStartupInfo;

    /* Determine exe source string */
    const char *exe = lpApplicationName;
    if (!exe || !exe[0])
        exe = lpCommandLine;
    if (!exe || !exe[0]) {
        last_error = 2;
        return FALSE;
    }

    /* Find a free child context slot */
    int slot = -1;
    for (int s = 0; s < MAX_CHILD_PROCS; s++) {
        if (!child_ctxs[s].in_use) { slot = s; break; }
    }
    if (slot < 0) return FALSE;

    /* Extract exe path directly into static ctx */
    child_ctx_t *ctx = &child_ctxs[slot];
    memset(ctx, 0, sizeof(*ctx));
    {
        const char *src = exe;
        if (src[0] == '"') src++;
        int n = 0;
        while (src[n] && src[n] != ' ' && src[n] != '"' && n < 63) {
            ctx->exe_path[n] = src[n];
            n++;
        }
        ctx->exe_path[n] = '\0';
    }

    DBG("CreateProcessA: exe='%s'", ctx->exe_path);

    /* Verify file exists — use non-stdcall helper (never call stdcall
     * shims internally — GCC register-passing breaks ret $N cleanup) */
    if (!file_exists(ctx->exe_path)) {
        DBG("CreateProcessA: file not found");
        memset(ctx, 0, sizeof(*ctx));
        last_error = 2;
        return FALSE;
    }

    /* Allocate handles */
    HANDLE hProc = alloc_handle(HTYPE_PROCESS);
    if (hProc == INVALID_HANDLE_VALUE) {
        memset(ctx, 0, sizeof(*ctx));
        return FALSE;
    }
    HANDLE hThread = alloc_handle(HTYPE_THREAD);
    if (hThread == INVALID_HANDLE_VALUE) {
        free_handle(hProc);
        memset(ctx, 0, sizeof(*ctx));
        return FALSE;
    }

    /* Create per-process page directory */
    uint32_t child_pd = vmm_create_user_pagedir();
    if (!child_pd) {
        DBG("CreateProcessA: vmm_create_user_pagedir failed");
        free_handle(hProc);
        free_handle(hThread);
        memset(ctx, 0, sizeof(*ctx));
        return FALSE;
    }
    DBG("CreateProcessA: created child PD=0x%x for '%s'", child_pd, ctx->exe_path);

    /* Fill context and handles */
    ctx->proc_handle = hProc;
    ctx->thread_handle = hThread;
    ctx->page_dir = child_pd;
    ctx->page_table = 0;
    ctx->num_frames = 0;
    ctx->ready = 0;
    ctx->in_use = 1;

    win32_handle_t *pwh = get_handle(hProc);
    pwh->proc_done = 0;
    pwh->proc_exit = 259;  /* STILL_ACTIVE */

    win32_handle_t *twh = get_handle(hThread);
    twh->thread_done = 0;
    twh->thread_exit = 0;

    /* Spawn the child */
    int tid = task_create_thread(ctx->exe_path, child_process_wrapper, 1);
    if (tid < 0) {
        memset(ctx, 0, sizeof(*ctx));
        free_handle(hProc);
        free_handle(hThread);
        return FALSE;
    }

    ctx->tid = tid;
    pwh->proc_tid = tid;
    twh->tid = tid;
    ctx->ready = 1;

    /* Fill output struct — use only small stack locals (pointers/ints) */
    if (lpProcessInformation) {
        lpProcessInformation->hProcess = hProc;
        lpProcessInformation->hThread = hThread;
        lpProcessInformation->dwProcessId = (DWORD)task_get_pid(tid);
        lpProcessInformation->dwThreadId = (DWORD)tid;
    }

    DBG("CreateProcessA: '%s' tid=%d hProc=%u hThread=%u",
        ctx->exe_path, tid, (unsigned)hProc, (unsigned)hThread);
    return TRUE;
}

static BOOL WINAPI shim_GetExitCodeProcess(HANDLE hProcess, LPDWORD lpExitCode) {
    win32_handle_t *wh = get_handle(hProcess);
    if (!wh || wh->type != HTYPE_PROCESS) return FALSE;
    if (lpExitCode) {
        *lpExitCode = wh->proc_done ? wh->proc_exit : 259; /* STILL_ACTIVE */
    }
    return TRUE;
}

static BOOL WINAPI shim_TerminateProcess(HANDLE hProcess, UINT uExitCode) {
    win32_handle_t *wh = get_handle(hProcess);
    if (!wh || wh->type != HTYPE_PROCESS) return FALSE;
    /* Kill the task */
    int pid = task_get_pid(wh->proc_tid);
    if (pid > 0) task_kill_by_pid(pid);
    wh->proc_exit = uExitCode;
    wh->proc_done = 1;
    return TRUE;
}

static HANDLE WINAPI shim_GetCurrentProcess(void) {
    return (HANDLE)0xFFFFFFFF;  /* pseudo-handle for current process */
}

static HANDLE WINAPI shim_OpenProcess(DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwProcessId) {
    (void)dwDesiredAccess; (void)bInheritHandle;
    /* Find the child process by PID */
    for (int i = 0; i < MAX_CHILD_PROCS; i++) {
        if (child_ctxs[i].in_use) {
            int pid = task_get_pid(child_ctxs[i].tid);
            if ((DWORD)pid == dwProcessId)
                return child_ctxs[i].proc_handle;
        }
    }
    return INVALID_HANDLE_VALUE;
}

static BOOL WINAPI shim_DuplicateHandle(
    HANDLE hSourceProcess, HANDLE hSourceHandle,
    HANDLE hTargetProcess, HANDLE *lpTargetHandle,
    DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwOptions)
{
    (void)hSourceProcess; (void)hTargetProcess;
    (void)dwDesiredAccess; (void)bInheritHandle; (void)dwOptions;

    /* Simple implementation: just copy the handle value.
     * In our single-address-space OS, handles are global anyway. */
    if (lpTargetHandle) {
        *lpTargetHandle = hSourceHandle;
    }
    return TRUE;
}

/* ── Anonymous Pipes (CreatePipe) ────────────────────────────── */

#define HTYPE_PIPE 12  /* extends handle_type_t */

static BOOL WINAPI shim_CreatePipe(
    HANDLE *hReadPipe, HANDLE *hWritePipe,
    LPVOID lpPipeAttributes, DWORD nSize)
{
    (void)lpPipeAttributes; (void)nSize;

    if (!hReadPipe || !hWritePipe) return FALSE;

    int read_fd, write_fd;
    int tid = task_get_current();
    if (pipe_create(&read_fd, &write_fd, tid) < 0) {
        return FALSE;
    }

    HANDLE hr = alloc_handle((handle_type_t)HTYPE_PIPE);
    HANDLE hw = alloc_handle((handle_type_t)HTYPE_PIPE);
    if (hr == INVALID_HANDLE_VALUE || hw == INVALID_HANDLE_VALUE) {
        pipe_close(read_fd, tid);
        pipe_close(write_fd, tid);
        if (hr != INVALID_HANDLE_VALUE) free_handle(hr);
        if (hw != INVALID_HANDLE_VALUE) free_handle(hw);
        return FALSE;
    }

    win32_handle_t *rh = get_handle(hr);
    rh->tid = read_fd;  /* reuse tid field for pipe fd */
    rh->signaled = 0;   /* 0 = read end */

    win32_handle_t *wh_pipe = get_handle(hw);
    wh_pipe->tid = write_fd;
    wh_pipe->signaled = 1;  /* 1 = write end */

    *hReadPipe = hr;
    *hWritePipe = hw;
    return TRUE;
}

/* ── Memory — VirtualAlloc with real page-backed allocations ── */

/* Track virtual allocations for VirtualQuery / VirtualFree */
#define MAX_VREGIONS 64
typedef struct {
    uint32_t base;      /* virtual base address */
    uint32_t size;      /* region size in bytes */
    uint32_t n_frames;  /* number of PMM frames allocated */
    uint32_t protect;   /* Win32 protection flags */
    int      in_use;
} vregion_t;

static vregion_t vregions[MAX_VREGIONS];

/* Next VirtualAlloc address — start at 0x10000000 to stay away from
 * kernel/heap space but inside identity-mapped 256MB */
static uint32_t valloc_next = 0x05000000;

/* Convert Win32 protection flags to PTE flags */
static uint32_t win32_prot_to_pte(DWORD protect) {
    uint32_t flags = PTE_PRESENT | PTE_USER;
    if (protect == PAGE_NOACCESS)
        return PTE_USER;  /* present=0, will fault */
    if (protect == PAGE_READONLY || protect == PAGE_EXECUTE_READ)
        return flags;  /* read-only (no PTE_WRITABLE) */
    return flags | PTE_WRITABLE;  /* PAGE_READWRITE and others */
}

static LPVOID WINAPI shim_VirtualAlloc(
    LPVOID lpAddress, DWORD dwSize, DWORD flAllocationType, DWORD flProtect)
{
    (void)flAllocationType;

    /* Round size up to page boundary */
    uint32_t pages = (dwSize + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages == 0) pages = 1;

    uint32_t base;
    if (lpAddress) {
        base = (uint32_t)lpAddress & PAGE_MASK;
    } else {
        /* Align next address to page boundary */
        base = (valloc_next + PAGE_SIZE - 1) & PAGE_MASK;
        valloc_next = base + pages * PAGE_SIZE;
        /* Safety: don't exceed identity-mapped region */
        if (valloc_next > 0x0F000000) {
            DBG("VirtualAlloc: out of virtual address space");
            return NULL;
        }
    }

    /* Find a free region slot */
    int slot = -1;
    for (int i = 0; i < MAX_VREGIONS; i++) {
        if (!vregions[i].in_use) { slot = i; break; }
    }
    if (slot < 0) {
        DBG("VirtualAlloc: no region slots");
        return NULL;
    }

    /* Allocate physical frames and map pages */
    uint32_t pte_flags = win32_prot_to_pte(flProtect);
    for (uint32_t i = 0; i < pages; i++) {
        uint32_t frame = pmm_alloc_frame();
        if (!frame) {
            /* Rollback: free already-allocated frames */
            for (uint32_t j = 0; j < i; j++) {
                uint32_t va = base + j * PAGE_SIZE;
                /* Read back the physical frame from the PTE to free it */
                uint32_t pde_idx = va >> 22;
                uint32_t pte_idx = (va >> 12) & 0x3FF;
                extern uint32_t kernel_page_tables[][1024];
                if (pde_idx < 64) {
                    uint32_t phys = kernel_page_tables[pde_idx][pte_idx] & PAGE_MASK;
                    pmm_free_frame(phys);
                }
                vmm_unmap_page(va);
            }
            return NULL;
        }
        /* Zero the frame */
        memset((void *)frame, 0, PAGE_SIZE);
        vmm_map_page(base + i * PAGE_SIZE, frame, pte_flags);
    }

    /* Record the region */
    vregions[slot].base = base;
    vregions[slot].size = pages * PAGE_SIZE;
    vregions[slot].n_frames = pages;
    vregions[slot].protect = flProtect;
    vregions[slot].in_use = 1;

    DBG("VirtualAlloc: base=0x%x size=%u pages=%u prot=0x%x",
        base, dwSize, pages, flProtect);
    return (LPVOID)base;
}

static BOOL WINAPI shim_VirtualFree(LPVOID lpAddress, DWORD dwSize, DWORD dwFreeType) {
    (void)dwSize; (void)dwFreeType;
    uint32_t addr = (uint32_t)lpAddress;

    for (int i = 0; i < MAX_VREGIONS; i++) {
        if (vregions[i].in_use && vregions[i].base == addr) {
            /* Free all frames */
            for (uint32_t j = 0; j < vregions[i].n_frames; j++) {
                uint32_t va = addr + j * PAGE_SIZE;
                uint32_t pde_idx = va >> 22;
                uint32_t pte_idx = (va >> 12) & 0x3FF;
                extern uint32_t kernel_page_tables[][1024];
                if (pde_idx < 64) {
                    uint32_t pte = kernel_page_tables[pde_idx][pte_idx];
                    if (pte & PTE_PRESENT)
                        pmm_free_frame(pte & PAGE_MASK);
                }
                vmm_unmap_page(va);
            }
            vregions[i].in_use = 0;
            return TRUE;
        }
    }
    /* Fallback: might be a malloc'd pointer from old code */
    free(lpAddress);
    return TRUE;
}

static BOOL WINAPI shim_VirtualProtect(
    LPVOID lpAddress, DWORD dwSize, DWORD flNewProtect, DWORD *lpflOldProtect)
{
    uint32_t addr = (uint32_t)lpAddress & PAGE_MASK;
    uint32_t pages = (dwSize + PAGE_SIZE - 1) / PAGE_SIZE;
    uint32_t pte_flags = win32_prot_to_pte(flNewProtect);

    /* Find the region to get old protect */
    DWORD old_prot = PAGE_READWRITE;
    for (int i = 0; i < MAX_VREGIONS; i++) {
        if (vregions[i].in_use &&
            addr >= vregions[i].base &&
            addr < vregions[i].base + vregions[i].size) {
            old_prot = vregions[i].protect;
            vregions[i].protect = flNewProtect;
            break;
        }
    }
    if (lpflOldProtect) *lpflOldProtect = old_prot;

    /* Update page table entries */
    extern uint32_t kernel_page_tables[][1024];
    for (uint32_t i = 0; i < pages; i++) {
        uint32_t va = addr + i * PAGE_SIZE;
        uint32_t pde_idx = va >> 22;
        uint32_t pte_idx = (va >> 12) & 0x3FF;
        if (pde_idx < 64) {
            uint32_t phys = kernel_page_tables[pde_idx][pte_idx] & PAGE_MASK;
            kernel_page_tables[pde_idx][pte_idx] = phys | pte_flags;
            vmm_invlpg(va);
        }
    }
    return TRUE;
}

/* MEMORY_BASIC_INFORMATION for VirtualQuery */
typedef struct {
    LPVOID BaseAddress;
    LPVOID AllocationBase;
    DWORD  AllocationProtect;
    DWORD  RegionSize;
    DWORD  State;
    DWORD  Protect;
    DWORD  Type;
} MEMORY_BASIC_INFORMATION;

static DWORD WINAPI shim_VirtualQuery(
    LPCVOID lpAddress, MEMORY_BASIC_INFORMATION *lpBuffer, DWORD dwLength)
{
    if (!lpBuffer || dwLength < sizeof(MEMORY_BASIC_INFORMATION))
        return 0;

    uint32_t addr = (uint32_t)lpAddress;
    memset(lpBuffer, 0, sizeof(MEMORY_BASIC_INFORMATION));

    for (int i = 0; i < MAX_VREGIONS; i++) {
        if (vregions[i].in_use &&
            addr >= vregions[i].base &&
            addr < vregions[i].base + vregions[i].size) {
            lpBuffer->BaseAddress = (LPVOID)vregions[i].base;
            lpBuffer->AllocationBase = (LPVOID)vregions[i].base;
            lpBuffer->AllocationProtect = vregions[i].protect;
            lpBuffer->RegionSize = vregions[i].size;
            lpBuffer->State = MEM_COMMIT;
            lpBuffer->Protect = vregions[i].protect;
            lpBuffer->Type = 0x20000;  /* MEM_PRIVATE */
            return sizeof(MEMORY_BASIC_INFORMATION);
        }
    }

    /* Address not in our tracked regions — report as free */
    lpBuffer->BaseAddress = (LPVOID)(addr & PAGE_MASK);
    lpBuffer->RegionSize = PAGE_SIZE;
    lpBuffer->State = 0x10000;  /* MEM_FREE */
    return sizeof(MEMORY_BASIC_INFORMATION);
}

/* ── Memory-mapped files ─────────────────────────────────────── */

#define HTYPE_FILEMAPPING 10  /* extends handle_type_t */

static HANDLE WINAPI shim_CreateFileMappingA(
    HANDLE hFile, LPVOID lpAttr, DWORD flProtect,
    DWORD dwMaxHigh, DWORD dwMaxLow, LPCSTR lpName)
{
    (void)lpAttr; (void)flProtect; (void)dwMaxHigh; (void)lpName;

    HANDLE h = alloc_handle((handle_type_t)HTYPE_FILEMAPPING);
    if (h == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

    win32_handle_t *wh = get_handle(h);
    /* Store the source file handle and desired size */
    wh->size = dwMaxLow;
    if (hFile != INVALID_HANDLE_VALUE) {
        win32_handle_t *fh = get_handle(hFile);
        if (fh && fh->buffer) {
            wh->buffer = fh->buffer;
            wh->size = fh->size;
        }
    }
    return h;
}

static LPVOID WINAPI shim_MapViewOfFile(
    HANDLE hMap, DWORD dwAccess, DWORD dwOffHigh, DWORD dwOffLow, DWORD dwBytes)
{
    (void)dwAccess; (void)dwOffHigh;

    win32_handle_t *wh = get_handle(hMap);
    if (!wh) return NULL;

    DWORD map_size = dwBytes ? dwBytes : wh->size;
    if (map_size == 0) map_size = PAGE_SIZE;

    /* Allocate pages for the mapping */
    LPVOID p = shim_VirtualAlloc(NULL, map_size, MEM_COMMIT, PAGE_READWRITE);
    if (!p) return NULL;

    /* Copy file data into the mapped region if available */
    if (wh->buffer && wh->size > 0) {
        uint32_t copy_size = wh->size - dwOffLow;
        if (copy_size > map_size) copy_size = map_size;
        memcpy(p, wh->buffer + dwOffLow, copy_size);
    }
    return p;
}

static BOOL WINAPI shim_UnmapViewOfFile(LPCVOID lpBase) {
    return shim_VirtualFree((LPVOID)lpBase, 0, MEM_RELEASE);
}

/* ── Heap (unchanged — wraps malloc) ─────────────────────────── */

static HANDLE WINAPI shim_GetProcessHeap(void) {
    return (HANDLE)1;
}

static HANDLE WINAPI shim_HeapCreate(DWORD flOptions, DWORD dwInitialSize, DWORD dwMaximumSize) {
    (void)flOptions; (void)dwInitialSize; (void)dwMaximumSize;
    return (HANDLE)1;
}

#define HEAP_ZERO_MEMORY 0x08

static LPVOID WINAPI shim_HeapAlloc(HANDLE hHeap, DWORD dwFlags, DWORD dwBytes) {
    (void)hHeap;
    void *p = malloc(dwBytes);
    if (p && (dwFlags & HEAP_ZERO_MEMORY))
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

/* ── GlobalAlloc / GlobalFree (legacy) ───────────────────────── */

#define GMEM_FIXED    0x0000
#define GMEM_MOVEABLE 0x0002
#define GMEM_ZEROINIT 0x0040
#define GPTR          (GMEM_FIXED | GMEM_ZEROINIT)

typedef HANDLE HGLOBAL;

static HGLOBAL WINAPI shim_GlobalAlloc(UINT uFlags, DWORD dwBytes) {
    void *p;
    if (uFlags & GMEM_ZEROINIT)
        p = calloc(1, dwBytes);
    else
        p = malloc(dwBytes);
    return (HGLOBAL)p;
}

static HGLOBAL WINAPI shim_GlobalFree(HGLOBAL hMem) {
    free((void *)hMem);
    return (HGLOBAL)0;
}

static LPVOID WINAPI shim_GlobalLock(HGLOBAL hMem) {
    return (LPVOID)hMem;  /* GMEM_FIXED: handle IS the pointer */
}

static BOOL WINAPI shim_GlobalUnlock(HGLOBAL hMem) {
    (void)hMem;
    return TRUE;
}

static DWORD WINAPI shim_GlobalSize(HGLOBAL hMem) {
    (void)hMem;
    return 0;  /* can't determine size from malloc */
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

/* ── Proper UTF-8 ↔ UTF-16 Conversion ────────────────────────── */

/* Convert UTF-8 string to UTF-16 (WCHAR).
 * utf8_len: -1 means null-terminated.
 * out_len: size of output buffer in WCHARs. If 0, returns required count.
 * Returns number of WCHARs written/needed (including null if utf8_len was -1). */
int win32_utf8_to_wchar(const char *utf8, int utf8_len, WCHAR *out, int out_len) {
    if (!utf8) return 0;

    const uint8_t *s = (const uint8_t *)utf8;
    int src_len = utf8_len;
    int include_null = 0;

    if (src_len < 0) {
        src_len = (int)strlen(utf8);
        include_null = 1;
    }

    int wi = 0;
    int si = 0;

    while (si < src_len) {
        uint32_t cp;
        uint8_t b = s[si];

        if (b < 0x80) {
            cp = b;
            si += 1;
        } else if ((b & 0xE0) == 0xC0) {
            if (si + 1 >= src_len) break;
            cp = ((b & 0x1F) << 6) | (s[si+1] & 0x3F);
            si += 2;
        } else if ((b & 0xF0) == 0xE0) {
            if (si + 2 >= src_len) break;
            cp = ((b & 0x0F) << 12) | ((s[si+1] & 0x3F) << 6) | (s[si+2] & 0x3F);
            si += 3;
        } else if ((b & 0xF8) == 0xF0) {
            if (si + 3 >= src_len) break;
            cp = ((b & 0x07) << 18) | ((s[si+1] & 0x3F) << 12)
               | ((s[si+2] & 0x3F) << 6) | (s[si+3] & 0x3F);
            si += 4;
        } else {
            cp = 0xFFFD; /* replacement char */
            si += 1;
        }

        /* Emit as UTF-16 */
        if (cp <= 0xFFFF) {
            if (out && wi < out_len) out[wi] = (WCHAR)cp;
            wi++;
        } else if (cp <= 0x10FFFF) {
            /* Surrogate pair */
            cp -= 0x10000;
            if (out && wi < out_len) out[wi] = (WCHAR)(0xD800 | (cp >> 10));
            wi++;
            if (out && wi < out_len) out[wi] = (WCHAR)(0xDC00 | (cp & 0x3FF));
            wi++;
        }
    }

    if (include_null) {
        if (out && wi < out_len) out[wi] = 0;
        wi++;
    }

    return wi;
}

/* Convert UTF-16 (WCHAR) string to UTF-8.
 * wstr_len: -1 means null-terminated.
 * out_len: size of output buffer in bytes. If 0, returns required count.
 * Returns number of bytes written/needed (including null if wstr_len was -1). */
int win32_wchar_to_utf8(const WCHAR *wstr, int wstr_len, char *out, int out_len) {
    if (!wstr) return 0;

    int src_len = wstr_len;
    int include_null = 0;

    if (src_len < 0) {
        src_len = 0;
        while (wstr[src_len]) src_len++;
        include_null = 1;
    }

    int bi = 0;
    int si = 0;

    while (si < src_len) {
        uint32_t cp = wstr[si++];

        /* Handle surrogate pairs */
        if (cp >= 0xD800 && cp <= 0xDBFF && si < src_len) {
            WCHAR lo = wstr[si];
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                si++;
            }
        }

        /* Encode as UTF-8 */
        if (cp < 0x80) {
            if (out && bi < out_len) out[bi] = (char)cp;
            bi++;
        } else if (cp < 0x800) {
            if (out && bi < out_len) out[bi] = (char)(0xC0 | (cp >> 6));
            bi++;
            if (out && bi < out_len) out[bi] = (char)(0x80 | (cp & 0x3F));
            bi++;
        } else if (cp < 0x10000) {
            if (out && bi < out_len) out[bi] = (char)(0xE0 | (cp >> 12));
            bi++;
            if (out && bi < out_len) out[bi] = (char)(0x80 | ((cp >> 6) & 0x3F));
            bi++;
            if (out && bi < out_len) out[bi] = (char)(0x80 | (cp & 0x3F));
            bi++;
        } else {
            if (out && bi < out_len) out[bi] = (char)(0xF0 | (cp >> 18));
            bi++;
            if (out && bi < out_len) out[bi] = (char)(0x80 | ((cp >> 12) & 0x3F));
            bi++;
            if (out && bi < out_len) out[bi] = (char)(0x80 | ((cp >> 6) & 0x3F));
            bi++;
            if (out && bi < out_len) out[bi] = (char)(0x80 | (cp & 0x3F));
            bi++;
        }
    }

    if (include_null) {
        if (out && bi < out_len) out[bi] = '\0';
        bi++;
    }

    return bi;
}

static int WINAPI shim_MultiByteToWideChar(
    UINT cp, DWORD flags, LPCSTR mb, int cbMB, void *wc, int cchWC)
{
    (void)cp; (void)flags;
    return win32_utf8_to_wchar(mb, cbMB, (WCHAR *)wc, cchWC);
}

static int WINAPI shim_WideCharToMultiByte(
    UINT cp, DWORD flags, const void *wc, int cchWC,
    LPSTR mb, int cbMB, LPCSTR defChar, LPVOID usedDef)
{
    (void)cp; (void)flags; (void)defChar; (void)usedDef;
    return win32_wchar_to_utf8((const WCHAR *)wc, cchWC, mb, cbMB);
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
    if (!wh) {
        DBG("WaitForSingleObject: invalid handle %u", (unsigned)hHandle);
        return WAIT_FAILED;
    }

    DBG("WaitForSingleObject: handle=%u type=%d timeout=%u", (unsigned)hHandle, wh->type, (unsigned)dwMilliseconds);

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

        case HTYPE_PROCESS:
            if (wh->proc_done) {
                DBG("WaitForSingleObject: process done, returning");
                return WAIT_OBJECT_0;
            }
            /* Also check if the task is no longer alive */
            if (task_get(wh->proc_tid) == NULL) {
                DBG("WaitForSingleObject: proc_tid=%d task dead, marking done", wh->proc_tid);
                wh->proc_done = 1;
                return WAIT_OBJECT_0;
            }
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

/* ── W-suffix Wrappers ───────────────────────────────────────── */

static HANDLE WINAPI shim_CreateFileW(
    LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
    LPVOID lpSecAttrs, DWORD dwCreation, DWORD dwFlags, HANDLE hTemplate)
{
    char narrow[MAX_PATH];
    win32_wchar_to_utf8(lpFileName, -1, narrow, sizeof(narrow));
    return shim_CreateFileA(narrow, dwDesiredAccess, dwShareMode,
                            lpSecAttrs, dwCreation, dwFlags, hTemplate);
}

static DWORD WINAPI shim_GetModuleFileNameW(HMODULE hModule, LPWSTR lpFilename, DWORD nSize) {
    char narrow[MAX_PATH];
    DWORD ret = shim_GetModuleFileNameA(hModule, narrow, MAX_PATH);
    if (ret > 0 && lpFilename && nSize > 0)
        win32_utf8_to_wchar(narrow, -1, lpFilename, (int)nSize);
    return ret;
}

static DWORD WINAPI shim_GetCurrentDirectoryW(DWORD nBufferLength, LPWSTR lpBuffer) {
    char narrow[MAX_PATH];
    DWORD ret = shim_GetCurrentDirectoryA(MAX_PATH, narrow);
    if (ret > 0 && lpBuffer && nBufferLength > 0)
        win32_utf8_to_wchar(narrow, -1, lpBuffer, (int)nBufferLength);
    return ret;
}

static BOOL WINAPI shim_SetCurrentDirectoryW(LPCWSTR lpPathName) {
    char narrow[MAX_PATH];
    win32_wchar_to_utf8(lpPathName, -1, narrow, sizeof(narrow));
    return shim_SetCurrentDirectoryA(narrow);
}

static DWORD WINAPI shim_GetTempPathW(DWORD nBufferLength, LPWSTR lpBuffer) {
    char narrow[MAX_PATH];
    DWORD ret = shim_GetTempPathA(MAX_PATH, narrow);
    if (ret > 0 && lpBuffer && nBufferLength > 0)
        win32_utf8_to_wchar(narrow, -1, lpBuffer, (int)nBufferLength);
    return ret;
}

static HANDLE WINAPI shim_FindFirstFileW(LPCWSTR lpFileName, WIN32_FIND_DATAW *lpFindData) {
    (void)lpFileName;
    if (!lpFindData) return INVALID_HANDLE_VALUE;

    WIN32_FIND_DATAA narrow_data;
    HANDLE h = shim_FindFirstFileA(NULL, &narrow_data);
    if (h == INVALID_HANDLE_VALUE) return h;

    /* Convert result to wide */
    memset(lpFindData, 0, sizeof(WIN32_FIND_DATAW));
    lpFindData->dwFileAttributes = narrow_data.dwFileAttributes;
    lpFindData->nFileSizeHigh = narrow_data.nFileSizeHigh;
    lpFindData->nFileSizeLow = narrow_data.nFileSizeLow;
    win32_utf8_to_wchar(narrow_data.cFileName, -1, lpFindData->cFileName, 260);
    return h;
}

static BOOL WINAPI shim_FindNextFileW(HANDLE hFindFile, WIN32_FIND_DATAW *lpFindData) {
    if (!lpFindData) return FALSE;

    WIN32_FIND_DATAA narrow_data;
    BOOL ret = shim_FindNextFileA(hFindFile, &narrow_data);
    if (!ret) return FALSE;

    memset(lpFindData, 0, sizeof(WIN32_FIND_DATAW));
    lpFindData->dwFileAttributes = narrow_data.dwFileAttributes;
    lpFindData->nFileSizeHigh = narrow_data.nFileSizeHigh;
    lpFindData->nFileSizeLow = narrow_data.nFileSizeLow;
    win32_utf8_to_wchar(narrow_data.cFileName, -1, lpFindData->cFileName, 260);
    return TRUE;
}

static BOOL WINAPI shim_DeleteFileW(LPCWSTR lpFileName) {
    char narrow[MAX_PATH];
    win32_wchar_to_utf8(lpFileName, -1, narrow, sizeof(narrow));
    return shim_DeleteFileA(narrow);
}

static BOOL WINAPI shim_CreateDirectoryW(LPCWSTR lpPathName, LPVOID lpSecAttrs) {
    char narrow[MAX_PATH];
    win32_wchar_to_utf8(lpPathName, -1, narrow, sizeof(narrow));
    return shim_CreateDirectoryA(narrow, lpSecAttrs);
}

static DWORD WINAPI shim_GetFullPathNameW(
    LPCWSTR lpFileName, DWORD nBufferLength, LPWSTR lpBuffer, LPWSTR *lpFilePart)
{
    char narrow_in[MAX_PATH], narrow_out[MAX_PATH];
    win32_wchar_to_utf8(lpFileName, -1, narrow_in, sizeof(narrow_in));
    DWORD ret = shim_GetFullPathNameA(narrow_in, MAX_PATH, narrow_out, NULL);
    if (ret > 0 && lpBuffer && nBufferLength > 0)
        win32_utf8_to_wchar(narrow_out, -1, lpBuffer, (int)nBufferLength);
    if (lpFilePart) *lpFilePart = NULL;
    return ret;
}

static DWORD WINAPI shim_GetEnvironmentVariableW(LPCWSTR lpName, LPWSTR lpBuffer, DWORD nSize) {
    (void)lpName; (void)lpBuffer; (void)nSize;
    return 0;
}

static BOOL WINAPI shim_SetEnvironmentVariableW(LPCWSTR lpName, LPCWSTR lpValue) {
    (void)lpName; (void)lpValue;
    return TRUE;
}

static void WINAPI shim_OutputDebugStringW(LPCWSTR lpOutputString) {
    if (!lpOutputString) return;
    char narrow[512];
    win32_wchar_to_utf8(lpOutputString, -1, narrow, sizeof(narrow));
    printf("[debug] %s\n", narrow);
}

static void WINAPI shim_OutputDebugStringA(LPCSTR lpOutputString) {
    if (!lpOutputString) return;
    printf("[debug] %s\n", lpOutputString);
}

static HMODULE WINAPI shim_GetModuleHandleW(LPCWSTR lpModuleName) {
    if (!lpModuleName) return shim_GetModuleHandleA(NULL);
    char narrow[MAX_PATH];
    win32_wchar_to_utf8(lpModuleName, -1, narrow, sizeof(narrow));
    return shim_GetModuleHandleA(narrow);
}

static HMODULE WINAPI shim_LoadLibraryW(LPCWSTR lpLibFileName) {
    char narrow[MAX_PATH];
    win32_wchar_to_utf8(lpLibFileName, -1, narrow, sizeof(narrow));
    return shim_LoadLibraryA(narrow);
}

/* ── SEH API Wrappers ────────────────────────────────────────── */

static LPTOP_LEVEL_EXCEPTION_FILTER WINAPI shim_SetUnhandledExceptionFilter(
    LPTOP_LEVEL_EXCEPTION_FILTER filter)
{
    return seh_SetUnhandledExceptionFilter(filter);
}

static LONG WINAPI shim_UnhandledExceptionFilter(EXCEPTION_POINTERS *ep) {
    return seh_UnhandledExceptionFilter(ep);
}

static void WINAPI shim_RaiseException(DWORD code, DWORD flags,
                                        DWORD nargs, const DWORD *args)
{
    seh_RaiseException(code, flags, nargs, args);
}

static void WINAPI shim_RtlUnwind(void *target_frame, void *target_ip,
                                   EXCEPTION_RECORD *er, DWORD return_value)
{
    seh_RtlUnwind(target_frame, target_ip, er, return_value);
}

/* ── Phase 13: System Info ────────────────────────────────────── */

typedef struct {
    union {
        DWORD dwOemId;
        struct { WORD wProcessorArchitecture; WORD wReserved; };
    };
    DWORD dwPageSize;
    LPVOID lpMinimumApplicationAddress;
    LPVOID lpMaximumApplicationAddress;
    DWORD dwActiveProcessorMask;
    DWORD dwNumberOfProcessors;
    DWORD dwProcessorType;
    DWORD dwAllocationGranularity;
    WORD  wProcessorLevel;
    WORD  wProcessorRevision;
} SYSTEM_INFO, *LPSYSTEM_INFO;

#define PROCESSOR_ARCHITECTURE_INTEL 0
#define PROCESSOR_INTEL_PENTIUM      586

static void WINAPI shim_GetSystemInfo(LPSYSTEM_INFO si) {
    if (!si) return;
    memset(si, 0, sizeof(*si));
    si->wProcessorArchitecture = PROCESSOR_ARCHITECTURE_INTEL;
    si->dwPageSize = 4096;
    si->lpMinimumApplicationAddress = (LPVOID)0x00010000;
    si->lpMaximumApplicationAddress = (LPVOID)0x7FFEFFFF;
    si->dwActiveProcessorMask = 1;
    si->dwNumberOfProcessors = 1;
    si->dwProcessorType = PROCESSOR_INTEL_PENTIUM;
    si->dwAllocationGranularity = 65536;
    si->wProcessorLevel = 6;
    si->wProcessorRevision = 0x0301;
}

static void WINAPI shim_GetNativeSystemInfo(LPSYSTEM_INFO si) {
    shim_GetSystemInfo(si);
}

/* ── Phase 13: Version Info ──────────────────────────────────── */

typedef struct {
    DWORD dwOSVersionInfoSize;
    DWORD dwMajorVersion;
    DWORD dwMinorVersion;
    DWORD dwBuildNumber;
    DWORD dwPlatformId;
    CHAR  szCSDVersion[128];
} OSVERSIONINFOA, *LPOSVERSIONINFOA;

typedef struct {
    DWORD dwOSVersionInfoSize;
    DWORD dwMajorVersion;
    DWORD dwMinorVersion;
    DWORD dwBuildNumber;
    DWORD dwPlatformId;
    CHAR  szCSDVersion[128];
    WORD  wServicePackMajor;
    WORD  wServicePackMinor;
    WORD  wSuiteMask;
    BYTE  wProductType;
    BYTE  wReserved;
} OSVERSIONINFOEXA, *LPOSVERSIONINFOEXA;

#define VER_PLATFORM_WIN32_NT    2
#define VER_NT_WORKSTATION       1

static BOOL WINAPI shim_GetVersionExA(LPOSVERSIONINFOA info) {
    if (!info) return FALSE;
    /* Report as Windows 10.0.19041 */
    info->dwMajorVersion = 10;
    info->dwMinorVersion = 0;
    info->dwBuildNumber = 19041;
    info->dwPlatformId = VER_PLATFORM_WIN32_NT;
    memset(info->szCSDVersion, 0, sizeof(info->szCSDVersion));

    /* If extended struct, fill extra fields */
    if (info->dwOSVersionInfoSize >= sizeof(OSVERSIONINFOEXA)) {
        LPOSVERSIONINFOEXA ex = (LPOSVERSIONINFOEXA)info;
        ex->wServicePackMajor = 0;
        ex->wServicePackMinor = 0;
        ex->wSuiteMask = 0;
        ex->wProductType = VER_NT_WORKSTATION;
        ex->wReserved = 0;
    }
    return TRUE;
}

static BOOL WINAPI shim_GetVersionExW(void *info) {
    /* Wide version — fill same fields, szCSDVersion stays zeroed */
    return shim_GetVersionExA((LPOSVERSIONINFOA)info);
}

static DWORD WINAPI shim_GetVersion(void) {
    /* Low word: major.minor, high word: build + platform */
    return (10) | (0 << 8) | (19041 << 16);
}

/* Processor features — report basic x86 features */
#define PF_COMPARE_EXCHANGE_DOUBLE  2
#define PF_MMX_INSTRUCTIONS_AVAILABLE 3
#define PF_XMMI_INSTRUCTIONS_AVAILABLE 6  /* SSE */
#define PF_XMMI64_INSTRUCTIONS_AVAILABLE 10 /* SSE2 */
#define PF_NX_ENABLED              12

static BOOL WINAPI shim_IsProcessorFeaturePresent(DWORD feature) {
    switch (feature) {
        case PF_COMPARE_EXCHANGE_DOUBLE: return TRUE;
        case PF_MMX_INSTRUCTIONS_AVAILABLE: return TRUE;
        case PF_XMMI_INSTRUCTIONS_AVAILABLE: return TRUE;
        case PF_XMMI64_INSTRUCTIONS_AVAILABLE: return TRUE;
        case PF_NX_ENABLED: return FALSE;
        default: return FALSE;
    }
}

/* ── Phase 13: Environment Variables (A versions) ────────────── */

static DWORD WINAPI shim_GetEnvironmentVariableA(LPCSTR lpName, LPSTR lpBuffer, DWORD nSize) {
    if (!lpName) return 0;
    const char *val = env_get(lpName);
    if (!val) {
        last_error = 203; /* ERROR_ENVVAR_NOT_FOUND */
        return 0;
    }
    DWORD len = (DWORD)strlen(val);
    if (!lpBuffer || nSize == 0)
        return len + 1; /* Required size including NUL */
    if (nSize <= len) {
        return len + 1; /* Buffer too small */
    }
    strcpy(lpBuffer, val);
    return len;
}

static BOOL WINAPI shim_SetEnvironmentVariableA(LPCSTR lpName, LPCSTR lpValue) {
    if (!lpName) return FALSE;
    if (lpValue)
        return env_set(lpName, lpValue) == 0;
    else
        return env_set(lpName, "") == 0; /* Delete = set empty */
}

/* ── Phase 13: FormatMessage ─────────────────────────────────── */

#define FORMAT_MESSAGE_FROM_SYSTEM    0x1000
#define FORMAT_MESSAGE_IGNORE_INSERTS 0x200
#define FORMAT_MESSAGE_ALLOCATE_BUFFER 0x100

static DWORD WINAPI shim_FormatMessageA(DWORD dwFlags, LPCVOID lpSource,
    DWORD dwMessageId, DWORD dwLanguageId,
    LPSTR lpBuffer, DWORD nSize, void *Arguments)
{
    (void)lpSource; (void)dwLanguageId; (void)Arguments;

    const char *msg = "Unknown error";
    switch (dwMessageId) {
        case 0:  msg = "The operation completed successfully."; break;
        case 2:  msg = "The system cannot find the file specified."; break;
        case 3:  msg = "The system cannot find the path specified."; break;
        case 5:  msg = "Access is denied."; break;
        case 6:  msg = "The handle is invalid."; break;
        case 8:  msg = "Not enough storage is available to process this command."; break;
        case 87: msg = "The parameter is incorrect."; break;
        case 122: msg = "The data area passed to a system call is too small."; break;
        case 203: msg = "The system could not find the environment option."; break;
        default: break;
    }

    DWORD len = (DWORD)strlen(msg);
    if (dwFlags & FORMAT_MESSAGE_ALLOCATE_BUFFER) {
        char *buf = (char *)malloc(len + 1);
        if (!buf) return 0;
        strcpy(buf, msg);
        *(char **)lpBuffer = buf;
        return len;
    }

    if (!lpBuffer || nSize == 0) return 0;
    DWORD copy = (len < nSize - 1) ? len : nSize - 1;
    memcpy(lpBuffer, msg, copy);
    lpBuffer[copy] = '\0';
    return copy;
}

/* ── Phase 13: Locale ────────────────────────────────────────── */

#define LOCALE_SYSTEM_DEFAULT  0x0800
#define LOCALE_USER_DEFAULT    0x0400
#define LOCALE_SENGLANGUAGE    0x1001
#define LOCALE_SENGCOUNTRY     0x1002
#define LOCALE_SISO639LANGNAME 0x0059
#define LOCALE_SISO3166CTRYNAME 0x005A
#define LOCALE_IDEFAULTANSICODEPAGE 0x1004
#define LOCALE_SDECIMAL        0x000E
#define LOCALE_STHOUSAND       0x000F

typedef DWORD LCID;
typedef DWORD LCTYPE;

static LCID WINAPI shim_GetUserDefaultLCID(void) {
    return 0x0409; /* en-US */
}

static LCID WINAPI shim_GetSystemDefaultLCID(void) {
    return 0x0409;
}

static int WINAPI shim_GetLocaleInfoA(LCID Locale, LCTYPE LCType,
                                       LPSTR lpLCData, int cchData)
{
    (void)Locale;
    const char *val = "";
    switch (LCType) {
        case LOCALE_SENGLANGUAGE:       val = "English"; break;
        case LOCALE_SENGCOUNTRY:        val = "United States"; break;
        case LOCALE_SISO639LANGNAME:    val = "en"; break;
        case LOCALE_SISO3166CTRYNAME:   val = "US"; break;
        case LOCALE_IDEFAULTANSICODEPAGE: val = "1252"; break;
        case LOCALE_SDECIMAL:           val = "."; break;
        case LOCALE_STHOUSAND:          val = ","; break;
        default: val = ""; break;
    }
    int len = (int)strlen(val) + 1;
    if (cchData == 0) return len;
    if (!lpLCData) return 0;
    int copy = (len <= cchData) ? len : cchData;
    memcpy(lpLCData, val, copy - 1);
    lpLCData[copy - 1] = '\0';
    return copy;
}

static int WINAPI shim_GetLocaleInfoW(LCID Locale, LCTYPE LCType,
                                       LPWSTR lpLCData, int cchData)
{
    /* Get narrow version, then widen */
    char narrow[128];
    int len = shim_GetLocaleInfoA(Locale, LCType, narrow, sizeof(narrow));
    if (cchData == 0) return len;
    if (!lpLCData) return 0;
    int copy = (len <= cchData) ? len : cchData;
    for (int i = 0; i < copy; i++)
        lpLCData[i] = (WCHAR)(unsigned char)narrow[i];
    return copy;
}

static UINT WINAPI shim_GetACP(void) { return 1252; } /* Windows-1252 (Western) */
static UINT WINAPI shim_GetOEMCP(void) { return 437; } /* DOS CP437 */

static BOOL WINAPI shim_IsValidCodePage(UINT cp) {
    return (cp == 437 || cp == 1252 || cp == 65001); /* CP437, 1252, UTF-8 */
}

/* ── Phase 13: Time ──────────────────────────────────────────── */

typedef struct {
    WORD wYear;
    WORD wMonth;
    WORD wDayOfWeek;
    WORD wDay;
    WORD wHour;
    WORD wMinute;
    WORD wSecond;
    WORD wMilliseconds;
} SYSTEMTIME, *LPSYSTEMTIME;

typedef struct { DWORD dwLowDateTime; DWORD dwHighDateTime; } FILETIME, *LPFILETIME;

typedef struct {
    LONG  Bias;              /* UTC = local + Bias (in minutes) */
    WCHAR StandardName[32];
    SYSTEMTIME StandardDate;
    LONG  StandardBias;
    WCHAR DaylightName[32];
    SYSTEMTIME DaylightDate;
    LONG  DaylightBias;
} TIME_ZONE_INFORMATION, *LPTIME_ZONE_INFORMATION;

#define TIME_ZONE_ID_UNKNOWN 0

static void fill_systemtime(LPSYSTEMTIME st) {
    datetime_t dt;
    config_get_datetime(&dt);
    memset(st, 0, sizeof(*st));
    st->wYear = dt.year;
    st->wMonth = dt.month;
    st->wDay = dt.day;
    st->wHour = dt.hour;
    st->wMinute = dt.minute;
    st->wSecond = dt.second;
    st->wDayOfWeek = 0; /* approximation */
    st->wMilliseconds = 0;
}

static void WINAPI shim_GetLocalTime(LPSYSTEMTIME st) {
    if (st) fill_systemtime(st);
}

static void WINAPI shim_GetSystemTime(LPSYSTEMTIME st) {
    if (st) fill_systemtime(st); /* We don't track UTC separately */
}

/* FILETIME epoch: 1601-01-01. We approximate with a fixed base. */
#define FILETIME_BASE_HIGH 0x01D6A000  /* ~2020 era */

static void WINAPI shim_GetSystemTimeAsFileTime_real(LPFILETIME ft) {
    if (!ft) return;
    /* Approximate: use PIT ticks as low part for uniqueness */
    extern volatile uint32_t pit_ticks;
    ft->dwHighDateTime = FILETIME_BASE_HIGH;
    ft->dwLowDateTime = pit_ticks * 83333; /* ~120Hz → 100ns units */
}

static BOOL WINAPI shim_FileTimeToSystemTime(const FILETIME *ft, LPSYSTEMTIME st) {
    (void)ft;
    if (st) fill_systemtime(st);
    return TRUE;
}

static BOOL WINAPI shim_SystemTimeToFileTime(const SYSTEMTIME *st, LPFILETIME ft) {
    (void)st;
    if (ft) {
        ft->dwHighDateTime = FILETIME_BASE_HIGH;
        ft->dwLowDateTime = 0;
    }
    return TRUE;
}

static BOOL WINAPI shim_FileTimeToLocalFileTime(const FILETIME *ft, LPFILETIME lft) {
    if (ft && lft) { *lft = *ft; }
    return TRUE;
}

static DWORD WINAPI shim_GetTimeZoneInformation(LPTIME_ZONE_INFORMATION tzi) {
    if (!tzi) return TIME_ZONE_ID_UNKNOWN;
    memset(tzi, 0, sizeof(*tzi));
    tzi->Bias = 0; /* UTC */
    /* StandardName: "UTC" as wide chars */
    tzi->StandardName[0] = 'U'; tzi->StandardName[1] = 'T';
    tzi->StandardName[2] = 'C'; tzi->StandardName[3] = 0;
    return TIME_ZONE_ID_UNKNOWN;
}

/* ── Phase 13: Thread Pool Stubs ─────────────────────────────── */

static BOOL WINAPI shim_QueueUserWorkItem(void *func, LPVOID ctx, DWORD flags) {
    (void)func; (void)ctx; (void)flags;
    /* Stub: run inline or ignore. Real impl would queue to thread pool. */
    return TRUE;
}

static HANDLE WINAPI shim_CreateTimerQueue(void) {
    return (HANDLE)0xABCD0001; /* Fake handle */
}

static BOOL WINAPI shim_DeleteTimerQueue(HANDLE queue) {
    (void)queue;
    return TRUE;
}

static BOOL WINAPI shim_CreateTimerQueueTimer(HANDLE *timer, HANDLE queue,
    void *callback, LPVOID param, DWORD dueTime, DWORD period, DWORD flags)
{
    (void)timer; (void)queue; (void)callback; (void)param;
    (void)dueTime; (void)period; (void)flags;
    if (timer) *timer = (HANDLE)0xABCD0002;
    return TRUE;
}

static BOOL WINAPI shim_DeleteTimerQueueTimer(HANDLE queue, HANDLE timer, HANDLE event) {
    (void)queue; (void)timer; (void)event;
    return TRUE;
}

/* ── Phase 13: Startup Info ──────────────────────────────────── */

static void WINAPI shim_GetStartupInfoA_real(STARTUPINFOA *si) {
    if (!si) return;
    memset(si, 0, sizeof(*si));
    si->cb = sizeof(STARTUPINFOA);
}

static void WINAPI shim_GetStartupInfoW_real(void *si) {
    /* Same layout, just zero it */
    if (si) memset(si, 0, 68); /* sizeof STARTUPINFOW */
}

/* ── Stubs ───────────────────────────────────────────────────── */

static DWORD WINAPI shim_stub_zero(void) { return 0; }

/* ── Export Table ─────────────────────────────────────────────── */

static const win32_export_entry_t kernel32_exports[] = {
    /* File I/O */
    { "CreateFileA",            (void *)shim_CreateFileA },
    { "ReadFile",               (void *)shim_ReadFile },
    { "WriteFile",              (void *)shim_WriteFile },
    { "CloseHandle",            (void *)shim_CloseHandle },
    { "GetStdHandle",           (void *)shim_GetStdHandle },

    /* Directory enumeration */
    { "FindFirstFileA",         (void *)shim_FindFirstFileA },
    { "FindFirstFileW",         (void *)shim_FindFirstFileW },
    { "FindNextFileA",          (void *)shim_FindNextFileA },
    { "FindNextFileW",          (void *)shim_FindNextFileW },
    { "FindClose",              (void *)shim_FindClose },

    { "CreateFileW",            (void *)shim_CreateFileW },

    /* File attributes & info */
    { "GetFileAttributesA",     (void *)shim_GetFileAttributesA },
    { "GetFileAttributesW",     (void *)shim_GetFileAttributesA },
    { "GetFileSize",            (void *)shim_GetFileSize },
    { "GetFileType",            (void *)shim_GetFileType },

    /* Random access */
    { "SetFilePointer",         (void *)shim_SetFilePointer },
    { "SetEndOfFile",           (void *)shim_SetEndOfFile },

    /* Directory ops */
    { "CreateDirectoryA",       (void *)shim_CreateDirectoryA },
    { "CreateDirectoryW",       (void *)shim_CreateDirectoryW },
    { "RemoveDirectoryA",       (void *)shim_RemoveDirectoryA },

    /* Temp files */
    { "GetTempPathA",           (void *)shim_GetTempPathA },
    { "GetTempPathW",           (void *)shim_GetTempPathW },
    { "GetTempFileNameA",       (void *)shim_GetTempFileNameA },

    /* File operations */
    { "DeleteFileA",            (void *)shim_DeleteFileA },
    { "DeleteFileW",            (void *)shim_DeleteFileW },
    { "MoveFileA",              (void *)shim_MoveFileA },
    { "CopyFileA",              (void *)shim_CopyFileA },

    /* Module / path queries */
    { "GetModuleFileNameA",     (void *)shim_GetModuleFileNameA },
    { "GetModuleFileNameW",     (void *)shim_GetModuleFileNameW },
    { "GetCurrentDirectoryA",   (void *)shim_GetCurrentDirectoryA },
    { "GetCurrentDirectoryW",   (void *)shim_GetCurrentDirectoryW },
    { "SetCurrentDirectoryA",   (void *)shim_SetCurrentDirectoryA },
    { "SetCurrentDirectoryW",   (void *)shim_SetCurrentDirectoryW },
    { "GetFullPathNameA",       (void *)shim_GetFullPathNameA },
    { "GetFullPathNameW",       (void *)shim_GetFullPathNameW },
    { "GetLongPathNameA",       (void *)shim_GetLongPathNameA },

    /* Overlapped I/O stubs */
    { "GetOverlappedResult",    (void *)shim_GetOverlappedResult },
    { "CancelIo",              (void *)shim_CancelIo },

    /* Process / Module */
    { "ExitProcess",            (void *)shim_ExitProcess },
    { "GetModuleHandleA",       (void *)shim_GetModuleHandleA },
    { "GetModuleHandleW",       (void *)shim_GetModuleHandleW },
    { "GetProcAddress",         (void *)shim_GetProcAddress },
    { "LoadLibraryA",           (void *)shim_LoadLibraryA },
    { "LoadLibraryW",           (void *)shim_LoadLibraryW },
    { "LoadLibraryExA",         (void *)shim_LoadLibraryExA },
    { "LoadLibraryExW",         (void *)shim_LoadLibraryExA },
    { "FreeLibrary",            (void *)shim_FreeLibrary },
    { "GetCommandLineA",        (void *)shim_GetCommandLineA },
    { "GetCommandLineW",        (void *)shim_GetCommandLineA },
    { "GetCurrentProcessId",    (void *)shim_GetCurrentProcessId },
    { "GetCurrentThreadId",     (void *)shim_GetCurrentThreadId },

    /* Process creation */
    { "CreateProcessA",         (void *)shim_CreateProcessA },
    { "CreateProcessW",         (void *)shim_CreateProcessA },
    { "GetExitCodeProcess",     (void *)shim_GetExitCodeProcess },
    { "TerminateProcess",       (void *)shim_TerminateProcess },
    { "GetCurrentProcess",      (void *)shim_GetCurrentProcess },
    { "OpenProcess",            (void *)shim_OpenProcess },
    { "DuplicateHandle",        (void *)shim_DuplicateHandle },

    /* Pipes */
    { "CreatePipe",             (void *)shim_CreatePipe },

    /* Memory — Virtual */
    { "VirtualAlloc",           (void *)shim_VirtualAlloc },
    { "VirtualFree",            (void *)shim_VirtualFree },
    { "VirtualProtect",         (void *)shim_VirtualProtect },
    { "VirtualQuery",           (void *)shim_VirtualQuery },

    /* Memory — File mapping */
    { "CreateFileMappingA",     (void *)shim_CreateFileMappingA },
    { "CreateFileMappingW",     (void *)shim_CreateFileMappingA },
    { "MapViewOfFile",          (void *)shim_MapViewOfFile },
    { "UnmapViewOfFile",        (void *)shim_UnmapViewOfFile },

    /* Memory — Heap */
    { "GetProcessHeap",         (void *)shim_GetProcessHeap },
    { "HeapCreate",             (void *)shim_HeapCreate },
    { "HeapAlloc",              (void *)shim_HeapAlloc },
    { "HeapFree",               (void *)shim_HeapFree },
    { "HeapReAlloc",            (void *)shim_HeapReAlloc },
    { "HeapSize",               (void *)shim_HeapSize },
    { "HeapDestroy",            (void *)shim_HeapDestroy },

    /* Memory — Global (legacy) */
    { "GlobalAlloc",            (void *)shim_GlobalAlloc },
    { "GlobalFree",             (void *)shim_GlobalFree },
    { "GlobalLock",             (void *)shim_GlobalLock },
    { "GlobalUnlock",           (void *)shim_GlobalUnlock },
    { "GlobalSize",             (void *)shim_GlobalSize },

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

    /* Environment */
    { "GetEnvironmentVariableW",  (void *)shim_GetEnvironmentVariableW },
    { "SetEnvironmentVariableW",  (void *)shim_SetEnvironmentVariableW },

    /* Debug output */
    { "OutputDebugStringA",       (void *)shim_OutputDebugStringA },
    { "OutputDebugStringW",       (void *)shim_OutputDebugStringW },

    /* SEH */
    { "SetUnhandledExceptionFilter", (void *)shim_SetUnhandledExceptionFilter },
    { "UnhandledExceptionFilter",   (void *)shim_UnhandledExceptionFilter },
    { "RaiseException",             (void *)shim_RaiseException },
    { "RtlUnwind",                  (void *)shim_RtlUnwind },

    /* System info */
    { "GetSystemInfo",              (void *)shim_GetSystemInfo },
    { "GetNativeSystemInfo",        (void *)shim_GetNativeSystemInfo },

    /* Version info */
    { "GetVersionExA",              (void *)shim_GetVersionExA },
    { "GetVersionExW",              (void *)shim_GetVersionExW },
    { "GetVersion",                 (void *)shim_GetVersion },
    { "IsProcessorFeaturePresent",  (void *)shim_IsProcessorFeaturePresent },

    /* Environment variables */
    { "GetEnvironmentVariableA",    (void *)shim_GetEnvironmentVariableA },
    { "SetEnvironmentVariableA",    (void *)shim_SetEnvironmentVariableA },
    { "GetEnvironmentVariableW",    (void *)shim_GetEnvironmentVariableW },
    { "SetEnvironmentVariableW",    (void *)shim_SetEnvironmentVariableW },

    /* FormatMessage */
    { "FormatMessageA",             (void *)shim_FormatMessageA },

    /* Locale */
    { "GetUserDefaultLCID",         (void *)shim_GetUserDefaultLCID },
    { "GetSystemDefaultLCID",       (void *)shim_GetSystemDefaultLCID },
    { "GetLocaleInfoA",             (void *)shim_GetLocaleInfoA },
    { "GetLocaleInfoW",             (void *)shim_GetLocaleInfoW },
    { "GetACP",                     (void *)shim_GetACP },
    { "GetOEMCP",                   (void *)shim_GetOEMCP },
    { "IsValidCodePage",            (void *)shim_IsValidCodePage },

    /* Time */
    { "GetLocalTime",               (void *)shim_GetLocalTime },
    { "GetSystemTime",              (void *)shim_GetSystemTime },
    { "GetSystemTimeAsFileTime",    (void *)shim_GetSystemTimeAsFileTime_real },
    { "FileTimeToSystemTime",       (void *)shim_FileTimeToSystemTime },
    { "SystemTimeToFileTime",       (void *)shim_SystemTimeToFileTime },
    { "FileTimeToLocalFileTime",    (void *)shim_FileTimeToLocalFileTime },
    { "GetTimeZoneInformation",     (void *)shim_GetTimeZoneInformation },
    /* Thread pool */
    { "QueueUserWorkItem",          (void *)shim_QueueUserWorkItem },
    { "CreateTimerQueue",           (void *)shim_CreateTimerQueue },
    { "DeleteTimerQueue",           (void *)shim_DeleteTimerQueue },
    { "CreateTimerQueueTimer",      (void *)shim_CreateTimerQueueTimer },
    { "DeleteTimerQueueTimer",      (void *)shim_DeleteTimerQueueTimer },

    /* Startup info */
    { "GetStartupInfoA",            (void *)shim_GetStartupInfoA_real },
    { "GetStartupInfoW",            (void *)shim_GetStartupInfoW_real },

    /* Stubs */
    { "IsDebuggerPresent",          (void *)shim_stub_zero },
};

const win32_dll_shim_t win32_kernel32 = {
    .dll_name = "kernel32.dll",
    .exports = kernel32_exports,
    .num_exports = sizeof(kernel32_exports) / sizeof(kernel32_exports[0]),
};
