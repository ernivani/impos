#include <kernel/win32_types.h>
#include <kernel/fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Shell32 shim ────────────────────────────────────────────
 * Provides SHGetFolderPath, SHCreateDirectoryEx, ShellExecute,
 * and other shell COM-adjacent functions.
 * ─────────────────────────────────────────────────────────── */

/* UTF-8 ↔ UTF-16 helpers from kernel32 */
extern int win32_utf8_to_wchar(const char *utf8, int utf8_len, WCHAR *out, int out_len);
extern int win32_wchar_to_utf8(const WCHAR *wstr, int wstr_len, char *out, int out_len);

/* ── CSIDL → Path dispatch ───────────────────────────────────── */

static const char *csidl_to_path(int csidl) {
    /* Mask off flags (CSIDL_FLAG_CREATE = 0x8000, etc.) */
    csidl &= 0xFF;

    switch (csidl) {
        case CSIDL_DESKTOP:         return "/home/user/Desktop";
        case CSIDL_PROGRAMS:        return "/home/user/Programs";
        case CSIDL_PERSONAL:        return "/home/user/Documents";
        case CSIDL_APPDATA:         return "/home/user/AppData/Roaming";
        case CSIDL_LOCAL_APPDATA:   return "/home/user/AppData/Local";
        case CSIDL_COMMON_APPDATA:  return "/home/user/AppData/Common";
        case CSIDL_WINDOWS:         return "C:\\Windows";
        case CSIDL_SYSTEM:          return "C:\\Windows\\System32";
        case CSIDL_PROGRAM_FILES:   return "C:\\Program Files";
        default:                    return "/home/user";
    }
}

/* ── SHGetFolderPathA ────────────────────────────────────────── */

static HRESULT WINAPI shim_SHGetFolderPathA(
    HWND hwnd, int csidl, HANDLE hToken, DWORD dwFlags, LPSTR pszPath)
{
    (void)hwnd; (void)hToken; (void)dwFlags;
    if (!pszPath) return E_POINTER;

    const char *path = csidl_to_path(csidl);
    strncpy(pszPath, path, MAX_PATH - 1);
    pszPath[MAX_PATH - 1] = '\0';
    return S_OK;
}

/* ── SHGetFolderPathW ────────────────────────────────────────── */

static HRESULT WINAPI shim_SHGetFolderPathW(
    HWND hwnd, int csidl, HANDLE hToken, DWORD dwFlags, LPWSTR pszPath)
{
    (void)hwnd; (void)hToken; (void)dwFlags;
    if (!pszPath) return E_POINTER;

    const char *path = csidl_to_path(csidl);
    win32_utf8_to_wchar(path, -1, pszPath, MAX_PATH);
    return S_OK;
}

/* ── SHGetSpecialFolderPathA ─────────────────────────────────── */

static BOOL WINAPI shim_SHGetSpecialFolderPathA(
    HWND hwnd, LPSTR pszPath, int csidl, BOOL fCreate)
{
    (void)fCreate;
    return SUCCEEDED(shim_SHGetFolderPathA(hwnd, csidl, 0, 0, pszPath));
}

/* ── SHCreateDirectoryExA ────────────────────────────────────── */

static int WINAPI shim_SHCreateDirectoryExA(HWND hwnd, LPCSTR pszPath, LPVOID psa) {
    (void)hwnd; (void)psa;
    if (!pszPath) return 1;
    if (fs_create_file(pszPath, 1 /* is_directory */) == 0)
        return 0;
    return 1;  /* ERROR_ALREADY_EXISTS or similar */
}

/* ── ShellExecuteA ───────────────────────────────────────────── */

static DWORD WINAPI shim_ShellExecuteA(
    HWND hwnd, LPCSTR lpOperation, LPCSTR lpFile,
    LPCSTR lpParameters, LPCSTR lpDirectory, int nShowCmd)
{
    (void)hwnd; (void)lpOperation; (void)lpFile;
    (void)lpParameters; (void)lpDirectory; (void)nShowCmd;
    /* Return > 32 = success */
    return 33;
}

/* ── CommandLineToArgvW ──────────────────────────────────────── */

static LPWSTR *WINAPI shim_CommandLineToArgvW(LPCWSTR lpCmdLine, int *pNumArgs) {
    if (!pNumArgs) return NULL;

    /* Convert wide to narrow */
    char narrow[512];
    if (!lpCmdLine || lpCmdLine[0] == 0) {
        *pNumArgs = 0;
        LPWSTR *result = (LPWSTR *)malloc(sizeof(LPWSTR));
        if (result) result[0] = NULL;
        return result;
    }

    win32_wchar_to_utf8(lpCmdLine, -1, narrow, sizeof(narrow));

    /* Count args (simple space split) */
    int argc = 0;
    const char *p = narrow;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        argc++;
        while (*p && *p != ' ') p++;
    }

    /* Allocate array + string storage in one block */
    size_t total = (argc + 1) * sizeof(LPWSTR) + strlen(narrow) * 2 + 256;
    LPWSTR *argv = (LPWSTR *)malloc(total);
    if (!argv) { *pNumArgs = 0; return NULL; }

    WCHAR *str_area = (WCHAR *)((uint8_t *)argv + (argc + 1) * sizeof(LPWSTR));

    /* Parse again and fill */
    p = narrow;
    int idx = 0;
    while (*p && idx < argc) {
        while (*p == ' ') p++;
        if (!*p) break;

        const char *start = p;
        while (*p && *p != ' ') p++;

        int len = (int)(p - start);
        argv[idx] = str_area;
        for (int i = 0; i < len; i++)
            str_area[i] = (WCHAR)(unsigned char)start[i];
        str_area[len] = 0;
        str_area += len + 1;
        idx++;
    }
    argv[argc] = NULL;
    *pNumArgs = argc;
    return argv;
}

/* ── SHFileOperationA ────────────────────────────────────────── */

static int WINAPI shim_SHFileOperationA(LPVOID lpFileOp) {
    (void)lpFileOp;
    return 0;
}

/* ── ExtractIconA ────────────────────────────────────────────── */

static HANDLE WINAPI shim_ExtractIconA(HINSTANCE hInst, LPCSTR pszExeFileName, UINT nIconIndex) {
    (void)hInst; (void)pszExeFileName; (void)nIconIndex;
    return (HANDLE)0;
}

/* ── Export Table ─────────────────────────────────────────────── */

static const win32_export_entry_t shell32_exports[] = {
    { "SHGetFolderPathA",        (void *)shim_SHGetFolderPathA },
    { "SHGetFolderPathW",        (void *)shim_SHGetFolderPathW },
    { "SHGetSpecialFolderPathA", (void *)shim_SHGetSpecialFolderPathA },
    { "SHCreateDirectoryExA",    (void *)shim_SHCreateDirectoryExA },
    { "ShellExecuteA",           (void *)shim_ShellExecuteA },
    { "CommandLineToArgvW",      (void *)shim_CommandLineToArgvW },
    { "SHFileOperationA",        (void *)shim_SHFileOperationA },
    { "ExtractIconA",            (void *)shim_ExtractIconA },
};

const win32_dll_shim_t win32_shell32 = {
    .dll_name = "shell32.dll",
    .exports = shell32_exports,
    .num_exports = sizeof(shell32_exports) / sizeof(shell32_exports[0]),
};
