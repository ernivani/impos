#include <kernel/win32_types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── COM / OLE shim ──────────────────────────────────────────
 * Provides COM initialization, CoTaskMem*, OLE clipboard/drag-drop
 * stubs, and a static IMalloc singleton. Enough for Chromium's
 * COM usage to not crash on startup.
 * ─────────────────────────────────────────────────────────── */

static int com_initialized = 0;

/* ── COM Initialization ──────────────────────────────────────── */

static HRESULT WINAPI shim_CoInitialize(LPVOID pvReserved) {
    (void)pvReserved;
    com_initialized = 1;
    return S_OK;
}

static HRESULT WINAPI shim_CoInitializeEx(LPVOID pvReserved, DWORD dwCoInit) {
    (void)pvReserved; (void)dwCoInit;
    com_initialized = 1;
    return S_OK;
}

static void WINAPI shim_CoUninitialize(void) {
    com_initialized = 0;
}

/* ── CoCreateInstance ────────────────────────────────────────── */

static HRESULT WINAPI shim_CoCreateInstance(
    REFCLSID rclsid, LPVOID pUnkOuter, DWORD dwClsCtx,
    REFIID riid, LPVOID *ppv)
{
    (void)rclsid; (void)pUnkOuter; (void)dwClsCtx; (void)riid;
    if (ppv) *ppv = NULL;
    return REGDB_E_CLASSNOTREG;
}

/* ── CoTaskMem* ──────────────────────────────────────────────── */

static LPVOID WINAPI shim_CoTaskMemAlloc(DWORD cb) {
    return malloc(cb);
}

static LPVOID WINAPI shim_CoTaskMemRealloc(LPVOID pv, DWORD cb) {
    return realloc(pv, cb);
}

static void WINAPI shim_CoTaskMemFree(LPVOID pv) {
    free(pv);
}

/* ── IMalloc Singleton ───────────────────────────────────────── */

/* Forward declarations for IMalloc vtable functions */
typedef struct IMalloc IMalloc;
typedef struct IMallocVtbl IMallocVtbl;

struct IMallocVtbl {
    HRESULT (WINAPI *QueryInterface)(IMalloc *self, REFIID riid, void **ppv);
    DWORD   (WINAPI *AddRef)(IMalloc *self);
    DWORD   (WINAPI *Release)(IMalloc *self);
    void *  (WINAPI *Alloc)(IMalloc *self, DWORD cb);
    void *  (WINAPI *Realloc)(IMalloc *self, void *pv, DWORD cb);
    void    (WINAPI *Free)(IMalloc *self, void *pv);
    DWORD   (WINAPI *GetSize)(IMalloc *self, void *pv);
    int     (WINAPI *DidAlloc)(IMalloc *self, void *pv);
    void    (WINAPI *HeapMinimize)(IMalloc *self);
};

struct IMalloc {
    const IMallocVtbl *lpVtbl;
};

static HRESULT WINAPI imalloc_QueryInterface(IMalloc *self, REFIID riid, void **ppv) {
    (void)self; (void)riid;
    if (ppv) *ppv = self;
    return S_OK;
}
static DWORD WINAPI imalloc_AddRef(IMalloc *self)  { (void)self; return 1; }
static DWORD WINAPI imalloc_Release(IMalloc *self)  { (void)self; return 1; }
static void *WINAPI imalloc_Alloc(IMalloc *self, DWORD cb) { (void)self; return malloc(cb); }
static void *WINAPI imalloc_Realloc(IMalloc *self, void *pv, DWORD cb) { (void)self; return realloc(pv, cb); }
static void  WINAPI imalloc_Free(IMalloc *self, void *pv) { (void)self; free(pv); }
static DWORD WINAPI imalloc_GetSize(IMalloc *self, void *pv) { (void)self; (void)pv; return 0; }
static int   WINAPI imalloc_DidAlloc(IMalloc *self, void *pv) { (void)self; (void)pv; return -1; }
static void  WINAPI imalloc_HeapMinimize(IMalloc *self) { (void)self; }

static const IMallocVtbl s_imalloc_vtbl = {
    imalloc_QueryInterface,
    imalloc_AddRef,
    imalloc_Release,
    imalloc_Alloc,
    imalloc_Realloc,
    imalloc_Free,
    imalloc_GetSize,
    imalloc_DidAlloc,
    imalloc_HeapMinimize,
};

static IMalloc s_imalloc = { &s_imalloc_vtbl };

static HRESULT WINAPI shim_CoGetMalloc(DWORD dwMemContext, IMalloc **ppMalloc) {
    (void)dwMemContext;
    if (!ppMalloc) return E_POINTER;
    *ppMalloc = &s_imalloc;
    return S_OK;
}

/* ── OLE Initialization ──────────────────────────────────────── */

static HRESULT WINAPI shim_OleInitialize(LPVOID pvReserved) {
    (void)pvReserved;
    com_initialized = 1;
    return S_OK;
}

static void WINAPI shim_OleUninitialize(void) {
    com_initialized = 0;
}

/* ── OLE Clipboard ───────────────────────────────────────────── */

static HRESULT WINAPI shim_OleSetClipboard(LPVOID pDataObj) {
    (void)pDataObj;
    return S_OK;
}

static HRESULT WINAPI shim_OleGetClipboard(LPVOID *ppDataObj) {
    if (ppDataObj) *ppDataObj = NULL;
    return E_FAIL;
}

static HRESULT WINAPI shim_OleFlushClipboard(void) {
    return S_OK;
}

/* ── Drag and Drop ───────────────────────────────────────────── */

static HRESULT WINAPI shim_RegisterDragDrop(HWND hwnd, LPVOID pDropTarget) {
    (void)hwnd; (void)pDropTarget;
    return S_OK;
}

static HRESULT WINAPI shim_RevokeDragDrop(HWND hwnd) {
    (void)hwnd;
    return S_OK;
}

static HRESULT WINAPI shim_DoDragDrop(
    LPVOID pDataObj, LPVOID pDropSource, DWORD dwOKEffects, DWORD *pdwEffect)
{
    (void)pDataObj; (void)pDropSource; (void)dwOKEffects;
    if (pdwEffect) *pdwEffect = 0;
    return E_NOTIMPL;
}

/* ── Misc COM Helpers ────────────────────────────────────────── */

static HRESULT WINAPI shim_PropVariantClear(LPVOID pvar) {
    if (pvar) memset(pvar, 0, 24); /* PROPVARIANT is ~24 bytes */
    return S_OK;
}

static HRESULT WINAPI shim_CLSIDFromString(LPCWSTR lpsz, CLSID *pclsid) {
    (void)lpsz;
    if (pclsid) memset(pclsid, 0, sizeof(CLSID));
    return E_FAIL;
}

static int WINAPI shim_StringFromGUID2(const GUID *rguid, LPWSTR lpsz, int cchMax) {
    if (!rguid || !lpsz || cchMax < 39) return 0;

    /* Build GUID string manually (our snprintf lacks %X uppercase support) */
    static const char hex[] = "0123456789ABCDEF";
    char tmp[40];
    int p = 0;

    tmp[p++] = '{';
    /* Data1: 8 hex digits */
    for (int i = 7; i >= 0; i--)
        tmp[p++] = hex[(rguid->Data1 >> (i * 4)) & 0xF];
    tmp[p++] = '-';
    /* Data2: 4 hex digits */
    for (int i = 3; i >= 0; i--)
        tmp[p++] = hex[(rguid->Data2 >> (i * 4)) & 0xF];
    tmp[p++] = '-';
    /* Data3: 4 hex digits */
    for (int i = 3; i >= 0; i--)
        tmp[p++] = hex[(rguid->Data3 >> (i * 4)) & 0xF];
    tmp[p++] = '-';
    /* Data4[0..1]: 4 hex digits */
    tmp[p++] = hex[(rguid->Data4[0] >> 4) & 0xF];
    tmp[p++] = hex[rguid->Data4[0] & 0xF];
    tmp[p++] = hex[(rguid->Data4[1] >> 4) & 0xF];
    tmp[p++] = hex[rguid->Data4[1] & 0xF];
    tmp[p++] = '-';
    /* Data4[2..7]: 12 hex digits */
    for (int i = 2; i < 8; i++) {
        tmp[p++] = hex[(rguid->Data4[i] >> 4) & 0xF];
        tmp[p++] = hex[rguid->Data4[i] & 0xF];
    }
    tmp[p++] = '}';
    tmp[p] = '\0';

    /* Convert to wide */
    for (int i = 0; i < p; i++)
        lpsz[i] = (WCHAR)tmp[i];
    lpsz[p] = 0;
    return p + 1;
}

/* ── Export Table ─────────────────────────────────────────────── */

static const win32_export_entry_t ole32_exports[] = {
    { "CoInitialize",       (void *)shim_CoInitialize },
    { "CoInitializeEx",     (void *)shim_CoInitializeEx },
    { "CoUninitialize",     (void *)shim_CoUninitialize },
    { "CoCreateInstance",   (void *)shim_CoCreateInstance },
    { "CoTaskMemAlloc",     (void *)shim_CoTaskMemAlloc },
    { "CoTaskMemRealloc",   (void *)shim_CoTaskMemRealloc },
    { "CoTaskMemFree",      (void *)shim_CoTaskMemFree },
    { "CoGetMalloc",        (void *)shim_CoGetMalloc },
    { "OleInitialize",      (void *)shim_OleInitialize },
    { "OleUninitialize",    (void *)shim_OleUninitialize },
    { "OleSetClipboard",    (void *)shim_OleSetClipboard },
    { "OleGetClipboard",    (void *)shim_OleGetClipboard },
    { "OleFlushClipboard",  (void *)shim_OleFlushClipboard },
    { "RegisterDragDrop",   (void *)shim_RegisterDragDrop },
    { "RevokeDragDrop",     (void *)shim_RevokeDragDrop },
    { "DoDragDrop",         (void *)shim_DoDragDrop },
    { "PropVariantClear",   (void *)shim_PropVariantClear },
    { "CLSIDFromString",    (void *)shim_CLSIDFromString },
    { "StringFromGUID2",    (void *)shim_StringFromGUID2 },
};

const win32_dll_shim_t win32_ole32 = {
    .dll_name = "ole32.dll",
    .exports = ole32_exports,
    .num_exports = sizeof(ole32_exports) / sizeof(ole32_exports[0]),
};
