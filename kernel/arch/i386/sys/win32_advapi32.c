#include <kernel/win32_types.h>
#include <kernel/crypto.h>
#include <kernel/user.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ── Registry Emulation ─────────────────────────────────────────
 * In-memory registry tree for Win32 PE applications.
 * Pre-populated with keys Chromium reads on startup.
 * ────────────────────────────────────────────────────────────── */

/* ── Constants ─────────────────────────────────────────────── */
#define MAX_REG_KEYS    128
#define MAX_REG_VALUES  256
#define MAX_KEY_NAME    128
#define MAX_VALUE_NAME  64
#define MAX_VALUE_DATA  256
#define MAX_REG_HANDLES 32

/* Registry types */
#define REG_NONE      0
#define REG_SZ        1
#define REG_EXPAND_SZ 2
#define REG_BINARY    3
#define REG_DWORD     4

/* Predefined root keys */
#define HKEY_CLASSES_ROOT   ((HKEY)0x80000000)
#define HKEY_CURRENT_USER   ((HKEY)0x80000001)
#define HKEY_LOCAL_MACHINE  ((HKEY)0x80000002)
#define HKEY_USERS          ((HKEY)0x80000003)
#define HKEY_CURRENT_CONFIG ((HKEY)0x80000005)

/* Handle base for opened subkeys */
#define REG_HANDLE_BASE  0xBEEF0000

/* Error codes */
#define ERROR_SUCCESS        0
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_MORE_DATA      234
#define ERROR_NO_MORE_ITEMS  259
#define ERROR_INVALID_HANDLE 6
#define ERROR_INVALID_PARAMETER 87

/* Key disposition */
#define REG_CREATED_NEW_KEY     1
#define REG_OPENED_EXISTING_KEY 2

/* Access rights (ignored, but defined for completeness) */
#define KEY_READ        0x20019
#define KEY_WRITE       0x20006
#define KEY_ALL_ACCESS  0xF003F
#define KEY_QUERY_VALUE 0x0001

typedef uint32_t HKEY;
typedef uint32_t REGSAM;
typedef uint32_t LSTATUS;

/* ── Data Structures ─────────────────────────────────────────── */

typedef struct {
    int  in_use;
    char name[MAX_KEY_NAME];    /* full path: "HKLM\\SOFTWARE\\Microsoft" */
    int  parent;                /* index of parent key, -1 for roots */
} reg_key_t;

typedef struct {
    int     in_use;
    int     key_idx;            /* which key owns this value */
    char    name[MAX_VALUE_NAME]; /* value name ("" = default) */
    DWORD   type;               /* REG_SZ, REG_DWORD, REG_BINARY */
    uint8_t data[MAX_VALUE_DATA]; /* raw data */
    DWORD   data_size;          /* actual size in bytes */
} reg_value_t;

typedef struct {
    int in_use;
    int key_idx;                /* index into reg_keys[] */
} reg_handle_t;

static reg_key_t    reg_keys[MAX_REG_KEYS];
static reg_value_t  reg_values[MAX_REG_VALUES];
static reg_handle_t reg_handles[MAX_REG_HANDLES];
static int          registry_initialized = 0;

/* ── Helper: case-insensitive string compare ─────────────── */
static int stricmp(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/* ── Internal: find or create a key by full path ─────────── */
static int reg_find_key(const char *full_path) {
    for (int i = 0; i < MAX_REG_KEYS; i++) {
        if (reg_keys[i].in_use && stricmp(reg_keys[i].name, full_path) == 0)
            return i;
    }
    return -1;
}

static int reg_create_key(const char *full_path, int parent) {
    int idx = reg_find_key(full_path);
    if (idx >= 0) return idx;

    for (int i = 0; i < MAX_REG_KEYS; i++) {
        if (!reg_keys[i].in_use) {
            reg_keys[i].in_use = 1;
            strncpy(reg_keys[i].name, full_path, MAX_KEY_NAME - 1);
            reg_keys[i].name[MAX_KEY_NAME - 1] = '\0';
            reg_keys[i].parent = parent;
            return i;
        }
    }
    return -1; /* full */
}

/* ── Internal: find or create a value ────────────────────── */
static int reg_find_value(int key_idx, const char *name) {
    for (int i = 0; i < MAX_REG_VALUES; i++) {
        if (reg_values[i].in_use && reg_values[i].key_idx == key_idx &&
            stricmp(reg_values[i].name, name) == 0)
            return i;
    }
    return -1;
}

static int reg_create_value(int key_idx, const char *name, DWORD type,
                            const void *data, DWORD data_size) {
    int idx = reg_find_value(key_idx, name);
    if (idx < 0) {
        for (int i = 0; i < MAX_REG_VALUES; i++) {
            if (!reg_values[i].in_use) { idx = i; break; }
        }
    }
    if (idx < 0) return -1;

    reg_values[idx].in_use = 1;
    reg_values[idx].key_idx = key_idx;
    strncpy(reg_values[idx].name, name, MAX_VALUE_NAME - 1);
    reg_values[idx].name[MAX_VALUE_NAME - 1] = '\0';
    reg_values[idx].type = type;
    DWORD copy = data_size;
    if (copy > MAX_VALUE_DATA) copy = MAX_VALUE_DATA;
    memcpy(reg_values[idx].data, data, copy);
    reg_values[idx].data_size = copy;
    return idx;
}

/* Helper: set a REG_SZ value */
static void reg_set_sz(int key_idx, const char *name, const char *value) {
    DWORD len = strlen(value) + 1; /* include NUL */
    reg_create_value(key_idx, name, REG_SZ, value, len);
}

/* Helper: set a REG_DWORD value */
static void reg_set_dword(int key_idx, const char *name, DWORD value) {
    reg_create_value(key_idx, name, REG_DWORD, &value, sizeof(DWORD));
}

/* ── Internal: build full path from root HKEY + subkey ───── */
static const char *root_prefix(HKEY hKey) {
    if (hKey == HKEY_LOCAL_MACHINE)  return "HKLM";
    if (hKey == HKEY_CURRENT_USER)   return "HKCU";
    if (hKey == HKEY_CLASSES_ROOT)   return "HKCR";
    if (hKey == HKEY_USERS)          return "HKU";
    if (hKey == HKEY_CURRENT_CONFIG) return "HKCC";
    return NULL;
}

static int build_full_path(char *out, size_t out_size, HKEY hKey, const char *subkey) {
    const char *prefix = root_prefix(hKey);
    if (prefix) {
        if (subkey && subkey[0])
            snprintf(out, out_size, "%s\\%s", prefix, subkey);
        else
            snprintf(out, out_size, "%s", prefix);
        return 0;
    }
    /* hKey is an opened handle — resolve to key index */
    uint32_t h = (uint32_t)hKey;
    if (h >= REG_HANDLE_BASE && h < REG_HANDLE_BASE + MAX_REG_HANDLES) {
        int slot = h - REG_HANDLE_BASE;
        if (reg_handles[slot].in_use) {
            int ki = reg_handles[slot].key_idx;
            if (subkey && subkey[0])
                snprintf(out, out_size, "%s\\%s", reg_keys[ki].name, subkey);
            else
                snprintf(out, out_size, "%s", reg_keys[ki].name);
            return 0;
        }
    }
    return -1;
}

/* ── Internal: resolve HKEY to key index ─────────────────── */
static int resolve_hkey(HKEY hKey) {
    /* Predefined root keys — find the root node */
    const char *prefix = root_prefix(hKey);
    if (prefix)
        return reg_find_key(prefix);

    /* Opened handle */
    uint32_t h = (uint32_t)hKey;
    if (h >= REG_HANDLE_BASE && h < REG_HANDLE_BASE + MAX_REG_HANDLES) {
        int slot = h - REG_HANDLE_BASE;
        if (reg_handles[slot].in_use)
            return reg_handles[slot].key_idx;
    }
    return -1;
}

/* ── Internal: allocate a handle ─────────────────────────── */
static HKEY alloc_handle(int key_idx) {
    for (int i = 0; i < MAX_REG_HANDLES; i++) {
        if (!reg_handles[i].in_use) {
            reg_handles[i].in_use = 1;
            reg_handles[i].key_idx = key_idx;
            return (HKEY)(REG_HANDLE_BASE + i);
        }
    }
    return (HKEY)0;
}

/* ── Internal: ensure parent keys exist along a path ─────── */
static int reg_ensure_path(const char *full_path) {
    /* Create all ancestor keys that don't exist yet */
    char buf[MAX_KEY_NAME];
    strncpy(buf, full_path, MAX_KEY_NAME - 1);
    buf[MAX_KEY_NAME - 1] = '\0';

    int parent = -1;
    char *p = buf;
    char *seg_start = p;

    while (1) {
        char *sep = strchr(p, '\\');
        if (sep) *sep = '\0';

        /* buf[0..seg] is the path up to this point */
        char partial[MAX_KEY_NAME];
        strncpy(partial, buf, MAX_KEY_NAME - 1);
        partial[MAX_KEY_NAME - 1] = '\0';
        if (sep) {
            /* Reconstruct partial path */
            *sep = '\\';
            size_t len = sep - buf;
            memcpy(partial, buf, len);
            partial[len] = '\0';
        }

        int idx = reg_find_key(partial);
        if (idx < 0) {
            idx = reg_create_key(partial, parent);
            if (idx < 0) return -1;
        }
        parent = idx;

        if (!sep) break;
        p = sep + 1;
        (void)seg_start;
        seg_start = p;
    }

    return parent; /* returns index of the deepest key */
}

/* ── Registry Init: pre-populate keys ────────────────────── */
void registry_init(void) {
    if (registry_initialized) return;
    registry_initialized = 1;

    memset(reg_keys, 0, sizeof(reg_keys));
    memset(reg_values, 0, sizeof(reg_values));
    memset(reg_handles, 0, sizeof(reg_handles));

    int ki;

    /* Root keys */
    reg_create_key("HKLM", -1);
    reg_create_key("HKCU", -1);
    reg_create_key("HKCR", -1);
    reg_create_key("HKU", -1);
    reg_create_key("HKCC", -1);

    /* HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion */
    ki = reg_ensure_path("HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion");
    if (ki >= 0) {
        reg_set_sz(ki, "CurrentBuild", "19045");
        reg_set_sz(ki, "CurrentVersion", "6.3");
        reg_set_sz(ki, "ProductName", "Windows 10 Pro");
        reg_set_sz(ki, "CSDVersion", "");
        reg_set_dword(ki, "CurrentMajorVersionNumber", 10);
        reg_set_dword(ki, "CurrentMinorVersionNumber", 0);
    }

    /* HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion */
    ki = reg_ensure_path("HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion");
    if (ki >= 0) {
        reg_set_sz(ki, "ProgramFilesDir", "C:\\Program Files");
        reg_set_sz(ki, "CommonFilesDir", "C:\\Program Files\\Common Files");
    }

    /* HKLM\SYSTEM\CurrentControlSet\Control\Nls\CodePage */
    ki = reg_ensure_path("HKLM\\SYSTEM\\CurrentControlSet\\Control\\Nls\\CodePage");
    if (ki >= 0) {
        reg_set_sz(ki, "ACP", "1252");
        reg_set_sz(ki, "OEMCP", "437");
    }

    /* HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Shell Folders */
    ki = reg_ensure_path("HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders");
    if (ki >= 0) {
        reg_set_sz(ki, "Local AppData", "C:\\Users\\user\\AppData\\Local");
        reg_set_sz(ki, "AppData", "C:\\Users\\user\\AppData\\Roaming");
        reg_set_sz(ki, "Desktop", "C:\\Users\\user\\Desktop");
        reg_set_sz(ki, "Personal", "C:\\Users\\user\\Documents");
    }

    /* HKCU\Software\Google\Chrome (empty — Chromium creates keys here) */
    reg_ensure_path("HKCU\\Software\\Google\\Chrome");

    /* HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts */
    ki = reg_ensure_path("HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts");
    if (ki >= 0) {
        reg_set_sz(ki, "Arial (TrueType)", "arial.ttf");
        reg_set_sz(ki, "Times New Roman (TrueType)", "times.ttf");
        reg_set_sz(ki, "Courier New (TrueType)", "cour.ttf");
        reg_set_sz(ki, "Segoe UI (TrueType)", "segoeui.ttf");
    }

    /* HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\FontSubstitutes */
    ki = reg_ensure_path("HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes");
    if (ki >= 0) {
        reg_set_sz(ki, "MS Shell Dlg", "Microsoft Sans Serif");
        reg_set_sz(ki, "MS Shell Dlg 2", "Segoe UI");
    }
}

/* ── API: RegOpenKeyExA ──────────────────────────────────── */
static LSTATUS WINAPI shim_RegOpenKeyExA(HKEY hKey, LPCSTR lpSubKey,
                                          DWORD ulOptions, REGSAM samDesired,
                                          HKEY *phkResult) {
    (void)ulOptions; (void)samDesired;
    if (!registry_initialized) registry_init();

    char full_path[MAX_KEY_NAME];
    if (build_full_path(full_path, sizeof(full_path), hKey, lpSubKey) < 0)
        return ERROR_INVALID_HANDLE;

    int ki = reg_find_key(full_path);
    if (ki < 0) return ERROR_FILE_NOT_FOUND;

    HKEY h = alloc_handle(ki);
    if (!h) return ERROR_INVALID_HANDLE;

    if (phkResult) *phkResult = h;
    return ERROR_SUCCESS;
}

/* ── API: RegCloseKey ────────────────────────────────────── */
static LSTATUS WINAPI shim_RegCloseKey(HKEY hKey) {
    /* Predefined keys are no-ops */
    uint32_t h = (uint32_t)hKey;
    if (h >= 0x80000000 && h <= 0x80000005)
        return ERROR_SUCCESS;

    if (h >= REG_HANDLE_BASE && h < REG_HANDLE_BASE + MAX_REG_HANDLES) {
        int slot = h - REG_HANDLE_BASE;
        if (reg_handles[slot].in_use) {
            reg_handles[slot].in_use = 0;
            return ERROR_SUCCESS;
        }
    }
    return ERROR_INVALID_HANDLE;
}

/* ── API: RegQueryValueExA ───────────────────────────────── */
static LSTATUS WINAPI shim_RegQueryValueExA(HKEY hKey, LPCSTR lpValueName,
                                             LPDWORD lpReserved, LPDWORD lpType,
                                             LPBYTE lpData, LPDWORD lpcbData) {
    (void)lpReserved;
    if (!registry_initialized) registry_init();

    int ki = resolve_hkey(hKey);
    if (ki < 0) return ERROR_INVALID_HANDLE;

    const char *vname = lpValueName ? lpValueName : "";
    int vi = reg_find_value(ki, vname);
    if (vi < 0) return ERROR_FILE_NOT_FOUND;

    reg_value_t *v = &reg_values[vi];

    if (lpType) *lpType = v->type;

    if (!lpcbData) {
        /* Caller just wants type */
        return ERROR_SUCCESS;
    }

    if (!lpData) {
        /* Caller wants required size */
        *lpcbData = v->data_size;
        return ERROR_SUCCESS;
    }

    if (*lpcbData < v->data_size) {
        *lpcbData = v->data_size;
        return ERROR_MORE_DATA;
    }

    memcpy(lpData, v->data, v->data_size);
    *lpcbData = v->data_size;
    return ERROR_SUCCESS;
}

/* ── API: RegEnumKeyExA ──────────────────────────────────── */
static LSTATUS WINAPI shim_RegEnumKeyExA(HKEY hKey, DWORD dwIndex,
                                          LPSTR lpName, LPDWORD lpcchName,
                                          LPDWORD lpReserved, LPSTR lpClass,
                                          LPDWORD lpcchClass, void *lpftLastWriteTime) {
    (void)lpReserved; (void)lpClass; (void)lpcchClass; (void)lpftLastWriteTime;
    if (!registry_initialized) registry_init();

    int parent_ki = resolve_hkey(hKey);
    if (parent_ki < 0) return ERROR_INVALID_HANDLE;

    /* Count children with matching parent */
    DWORD found = 0;
    for (int i = 0; i < MAX_REG_KEYS; i++) {
        if (reg_keys[i].in_use && reg_keys[i].parent == parent_ki) {
            if (found == dwIndex) {
                /* Extract the last component of the name */
                const char *full = reg_keys[i].name;
                const char *last_sep = strrchr(full, '\\');
                const char *child_name = last_sep ? last_sep + 1 : full;

                DWORD name_len = strlen(child_name);
                if (lpcchName && *lpcchName <= name_len)
                    return ERROR_MORE_DATA;
                if (lpName) {
                    strcpy(lpName, child_name);
                }
                if (lpcchName) *lpcchName = name_len;
                return ERROR_SUCCESS;
            }
            found++;
        }
    }
    return ERROR_NO_MORE_ITEMS;
}

/* ── API: RegEnumValueA ──────────────────────────────────── */
static LSTATUS WINAPI shim_RegEnumValueA(HKEY hKey, DWORD dwIndex,
                                          LPSTR lpValueName, LPDWORD lpcchValueName,
                                          LPDWORD lpReserved, LPDWORD lpType,
                                          LPBYTE lpData, LPDWORD lpcbData) {
    (void)lpReserved;
    if (!registry_initialized) registry_init();

    int ki = resolve_hkey(hKey);
    if (ki < 0) return ERROR_INVALID_HANDLE;

    DWORD found = 0;
    for (int i = 0; i < MAX_REG_VALUES; i++) {
        if (reg_values[i].in_use && reg_values[i].key_idx == ki) {
            if (found == dwIndex) {
                reg_value_t *v = &reg_values[i];

                DWORD name_len = strlen(v->name);
                if (lpcchValueName && *lpcchValueName <= name_len)
                    return ERROR_MORE_DATA;
                if (lpValueName) strcpy(lpValueName, v->name);
                if (lpcchValueName) *lpcchValueName = name_len;
                if (lpType) *lpType = v->type;

                if (lpcbData) {
                    if (lpData && *lpcbData >= v->data_size) {
                        memcpy(lpData, v->data, v->data_size);
                    } else if (lpData) {
                        *lpcbData = v->data_size;
                        return ERROR_MORE_DATA;
                    }
                    *lpcbData = v->data_size;
                }
                return ERROR_SUCCESS;
            }
            found++;
        }
    }
    return ERROR_NO_MORE_ITEMS;
}

/* ── API: RegCreateKeyExA ────────────────────────────────── */
static LSTATUS WINAPI shim_RegCreateKeyExA(HKEY hKey, LPCSTR lpSubKey,
                                            DWORD Reserved, LPSTR lpClass,
                                            DWORD dwOptions, REGSAM samDesired,
                                            void *lpSecurityAttributes,
                                            HKEY *phkResult, LPDWORD lpdwDisposition) {
    (void)Reserved; (void)lpClass; (void)dwOptions;
    (void)samDesired; (void)lpSecurityAttributes;
    if (!registry_initialized) registry_init();

    char full_path[MAX_KEY_NAME];
    if (build_full_path(full_path, sizeof(full_path), hKey, lpSubKey) < 0)
        return ERROR_INVALID_HANDLE;

    int existing = reg_find_key(full_path);
    int ki;

    if (existing >= 0) {
        ki = existing;
        if (lpdwDisposition) *lpdwDisposition = REG_OPENED_EXISTING_KEY;
    } else {
        ki = reg_ensure_path(full_path);
        if (ki < 0) return ERROR_INVALID_HANDLE;
        if (lpdwDisposition) *lpdwDisposition = REG_CREATED_NEW_KEY;
    }

    HKEY h = alloc_handle(ki);
    if (!h) return ERROR_INVALID_HANDLE;

    if (phkResult) *phkResult = h;
    return ERROR_SUCCESS;
}

/* ── API: RegSetValueExA ─────────────────────────────────── */
static LSTATUS WINAPI shim_RegSetValueExA(HKEY hKey, LPCSTR lpValueName,
                                           DWORD Reserved, DWORD dwType,
                                           const BYTE *lpData, DWORD cbData) {
    (void)Reserved;
    if (!registry_initialized) registry_init();

    int ki = resolve_hkey(hKey);
    if (ki < 0) return ERROR_INVALID_HANDLE;

    const char *vname = lpValueName ? lpValueName : "";
    int vi = reg_create_value(ki, vname, dwType, lpData, cbData);
    if (vi < 0) return ERROR_INVALID_HANDLE;

    return ERROR_SUCCESS;
}

/* ── Stubs ───────────────────────────────────────────────── */
static LSTATUS WINAPI shim_RegDeleteKeyA(HKEY hKey, LPCSTR lpSubKey) {
    (void)hKey; (void)lpSubKey;
    return ERROR_SUCCESS;
}

static LSTATUS WINAPI shim_RegDeleteValueA(HKEY hKey, LPCSTR lpValueName) {
    (void)hKey; (void)lpValueName;
    return ERROR_SUCCESS;
}

static LSTATUS WINAPI shim_RegNotifyChangeKeyValue(HKEY hKey, BOOL bWatchSubtree,
                                                    DWORD dwNotifyFilter, HANDLE hEvent,
                                                    BOOL fAsynchronous) {
    (void)hKey; (void)bWatchSubtree; (void)dwNotifyFilter;
    (void)hEvent; (void)fAsynchronous;
    return ERROR_SUCCESS;
}

/* ── Stubs: security APIs (Phase 13) ─────────────────────── */
static BOOL WINAPI shim_OpenProcessToken(HANDLE proc, DWORD access, HANDLE *tok) {
    (void)proc; (void)access;
    if (tok) *tok = 0xDEAD0001;
    return TRUE;
}

static BOOL WINAPI shim_GetTokenInformation(HANDLE tok, DWORD cls, LPVOID info,
                                             DWORD len, LPDWORD ret_len) {
    (void)tok; (void)cls; (void)info; (void)len;
    if (ret_len) *ret_len = 0;
    return FALSE;
}

static BOOL WINAPI shim_GetUserNameA(LPSTR buf, LPDWORD size) {
    const char *name = user_get_current();
    if (!name) name = "user";
    DWORD needed = (DWORD)strlen(name) + 1;
    if (!size) return FALSE;
    if (!buf || *size < needed) {
        *size = needed;
        return FALSE;
    }
    strcpy(buf, name);
    *size = needed;
    return TRUE;
}

static BOOL WINAPI shim_GetUserNameW(LPWSTR buf, LPDWORD size) {
    const char *name = user_get_current();
    if (!name) name = "user";
    DWORD needed = (DWORD)strlen(name) + 1;
    if (!size) return FALSE;
    if (!buf || *size < needed) {
        *size = needed;
        return FALSE;
    }
    for (DWORD i = 0; i < needed; i++)
        buf[i] = (WCHAR)(unsigned char)name[i];
    *size = needed;
    return TRUE;
}

/* ── CryptAPI (legacy advapi32 crypto) ───────────────────────── */

#define CRYPT_PROVIDER_HANDLE 0xCAAA0001

static BOOL WINAPI shim_CryptAcquireContextA(
    HANDLE *phProv, LPCSTR szContainer, LPCSTR szProvider,
    DWORD dwProvType, DWORD dwFlags)
{
    (void)szContainer; (void)szProvider; (void)dwProvType; (void)dwFlags;
    if (phProv) *phProv = CRYPT_PROVIDER_HANDLE;
    return TRUE;
}

static BOOL WINAPI shim_CryptAcquireContextW(
    HANDLE *phProv, const WCHAR *szContainer, const WCHAR *szProvider,
    DWORD dwProvType, DWORD dwFlags)
{
    (void)szContainer; (void)szProvider; (void)dwProvType; (void)dwFlags;
    if (phProv) *phProv = CRYPT_PROVIDER_HANDLE;
    return TRUE;
}

static BOOL WINAPI shim_CryptReleaseContext(HANDLE hProv, DWORD dwFlags) {
    (void)hProv; (void)dwFlags;
    return TRUE;
}

static BOOL WINAPI shim_CryptGenRandom(HANDLE hProv, DWORD dwLen, BYTE *pbBuffer) {
    (void)hProv;
    if (!pbBuffer || dwLen == 0) return FALSE;
    prng_random(pbBuffer, dwLen);
    return TRUE;
}

static BOOL WINAPI shim_CryptEncrypt(
    HANDLE hKey, HANDLE hHash, BOOL Final, DWORD dwFlags,
    BYTE *pbData, DWORD *pdwDataLen, DWORD dwBufLen)
{
    (void)hKey; (void)hHash; (void)Final; (void)dwFlags;
    (void)pbData; (void)pdwDataLen; (void)dwBufLen;
    return FALSE;
}

/* ── Export Table ─────────────────────────────────────────── */
static const win32_export_entry_t advapi32_exports[] = {
    { "CryptAcquireContextA",     (void *)shim_CryptAcquireContextA },
    { "CryptAcquireContextW",     (void *)shim_CryptAcquireContextW },
    { "CryptEncrypt",             (void *)shim_CryptEncrypt },
    { "CryptGenRandom",           (void *)shim_CryptGenRandom },
    { "CryptReleaseContext",      (void *)shim_CryptReleaseContext },
    { "GetTokenInformation",      (void *)shim_GetTokenInformation },
    { "GetUserNameA",             (void *)shim_GetUserNameA },
    { "GetUserNameW",             (void *)shim_GetUserNameW },
    { "OpenProcessToken",         (void *)shim_OpenProcessToken },
    { "RegCloseKey",              (void *)shim_RegCloseKey },
    { "RegCreateKeyExA",          (void *)shim_RegCreateKeyExA },
    { "RegDeleteKeyA",            (void *)shim_RegDeleteKeyA },
    { "RegDeleteValueA",          (void *)shim_RegDeleteValueA },
    { "RegEnumKeyExA",            (void *)shim_RegEnumKeyExA },
    { "RegEnumValueA",            (void *)shim_RegEnumValueA },
    { "RegNotifyChangeKeyValue",  (void *)shim_RegNotifyChangeKeyValue },
    { "RegOpenKeyExA",            (void *)shim_RegOpenKeyExA },
    { "RegQueryValueExA",         (void *)shim_RegQueryValueExA },
    { "RegSetValueExA",           (void *)shim_RegSetValueExA },
};

const win32_dll_shim_t win32_advapi32 = {
    .dll_name = "advapi32.dll",
    .exports = advapi32_exports,
    .num_exports = sizeof(advapi32_exports) / sizeof(advapi32_exports[0]),
};
