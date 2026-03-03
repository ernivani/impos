#include <kernel/test.h>
#include <kernel/win32_types.h>
#include <kernel/win32_seh.h>
#include <kernel/pe_loader.h>
#include <kernel/env.h>
#include <kernel/task.h>
#include <kernel/sched.h>
#include <kernel/vmm.h>
#include <kernel/fs.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>

static void test_win32_dll(void) {
    printf("== Win32 DLL Loading Tests ==\n");

    /* Resolve LoadLibraryA, GetProcAddress, FreeLibrary via shim tables */
    typedef uint32_t (__attribute__((stdcall)) *pfn_LoadLibraryA)(const char *);
    typedef void *   (__attribute__((stdcall)) *pfn_GetProcAddress)(uint32_t, const char *);
    typedef int      (__attribute__((stdcall)) *pfn_FreeLibrary)(uint32_t);

    pfn_LoadLibraryA  pLoadLibraryA  = (pfn_LoadLibraryA)win32_resolve_import("kernel32.dll", "LoadLibraryA");
    pfn_GetProcAddress pGetProcAddress = (pfn_GetProcAddress)win32_resolve_import("kernel32.dll", "GetProcAddress");
    pfn_FreeLibrary   pFreeLibrary   = (pfn_FreeLibrary)win32_resolve_import("kernel32.dll", "FreeLibrary");

    TEST_ASSERT(pLoadLibraryA != NULL,  "dll: LoadLibraryA resolved");
    TEST_ASSERT(pGetProcAddress != NULL, "dll: GetProcAddress resolved");
    TEST_ASSERT(pFreeLibrary != NULL,    "dll: FreeLibrary resolved");

    if (!pLoadLibraryA || !pGetProcAddress || !pFreeLibrary) return;

    /* LoadLibraryA("kernel32.dll") should return a non-null shim handle */
    uint32_t hKernel32 = pLoadLibraryA("kernel32.dll");
    TEST_ASSERT(hKernel32 != 0, "dll: LoadLibraryA(kernel32.dll) non-null");

    /* GetProcAddress on the loaded handle should find ExitProcess */
    void *pExitProcess = pGetProcAddress(hKernel32, "ExitProcess");
    TEST_ASSERT(pExitProcess != NULL, "dll: GetProcAddress(ExitProcess) found");

    /* GetProcAddress should find GetLastError too */
    void *pGetLastError = pGetProcAddress(hKernel32, "GetLastError");
    TEST_ASSERT(pGetLastError != NULL, "dll: GetProcAddress(GetLastError) found");

    /* Verify it matches the direct shim resolution */
    void *pDirect = win32_resolve_import("kernel32.dll", "ExitProcess");
    TEST_ASSERT(pExitProcess == pDirect, "dll: GetProcAddress matches direct resolve");

    /* LoadLibraryA for a different shim DLL */
    uint32_t hMsvcrt = pLoadLibraryA("msvcrt.dll");
    TEST_ASSERT(hMsvcrt != 0, "dll: LoadLibraryA(msvcrt.dll) non-null");

    void *pPrintf = pGetProcAddress(hMsvcrt, "printf");
    TEST_ASSERT(pPrintf != NULL, "dll: GetProcAddress(printf) from msvcrt");

    /* Loading same DLL twice should bump refcount and return same handle */
    uint32_t hKernel32_2 = pLoadLibraryA("kernel32.dll");
    TEST_ASSERT(hKernel32_2 == hKernel32, "dll: repeated LoadLibrary same handle");

    /* Case-insensitive loading */
    uint32_t hKernel32_upper = pLoadLibraryA("KERNEL32.DLL");
    TEST_ASSERT(hKernel32_upper == hKernel32, "dll: case-insensitive LoadLibrary");

    /* GetProcAddress for non-existent function should return NULL */
    void *pBogus = pGetProcAddress(hKernel32, "ThisFunctionDoesNotExist12345");
    TEST_ASSERT(pBogus == NULL, "dll: GetProcAddress(bogus) returns NULL");

    /* FreeLibrary should succeed */
    int freed = pFreeLibrary(hKernel32);
    TEST_ASSERT(freed != 0, "dll: FreeLibrary(kernel32) succeeds");

    /* Free the extra refs */
    pFreeLibrary(hKernel32);
    pFreeLibrary(hKernel32);
    pFreeLibrary(hMsvcrt);

    /* LoadLibraryA with NULL should return 0 */
    uint32_t hNull = pLoadLibraryA(NULL);
    TEST_ASSERT(hNull == 0, "dll: LoadLibraryA(NULL) returns 0");

    /* api-ms-win-crt-* should map to ucrtbase/msvcrt shims */
    uint32_t hApiMs = pLoadLibraryA("api-ms-win-crt-runtime-l1-1-0.dll");
    TEST_ASSERT(hApiMs != 0, "dll: LoadLibraryA(api-ms-win-crt-*) non-null");

    void *pMalloc = pGetProcAddress(hApiMs, "malloc");
    TEST_ASSERT(pMalloc != NULL, "dll: GetProcAddress(malloc) from api-ms-win-crt");

    pFreeLibrary(hApiMs);

    /* LoadLibraryExA should also work */
    typedef uint32_t (__attribute__((stdcall)) *pfn_LoadLibraryExA)(const char *, uint32_t, uint32_t);
    pfn_LoadLibraryExA pLoadLibraryExA = (pfn_LoadLibraryExA)win32_resolve_import("kernel32.dll", "LoadLibraryExA");
    TEST_ASSERT(pLoadLibraryExA != NULL, "dll: LoadLibraryExA resolved");

    if (pLoadLibraryExA) {
        uint32_t hEx = pLoadLibraryExA("user32.dll", 0, 0);
        TEST_ASSERT(hEx != 0, "dll: LoadLibraryExA(user32.dll) non-null");
        pFreeLibrary(hEx);
    }
}

/* ---- Win32 Registry Tests ---- */

static void test_win32_registry(void) {
    printf("== Win32 Registry Tests ==\n");

    /* Force re-init for clean state */
    registry_init();

    /* Typedefs matching the stdcall shim signatures */
    typedef uint32_t (__attribute__((stdcall)) *pfn_RegOpenKeyExA)(
        uint32_t hKey, const char *sub, uint32_t opts, uint32_t sam, uint32_t *out);
    typedef uint32_t (__attribute__((stdcall)) *pfn_RegCloseKey)(uint32_t hKey);
    typedef uint32_t (__attribute__((stdcall)) *pfn_RegQueryValueExA)(
        uint32_t hKey, const char *name, uint32_t *res, uint32_t *type,
        uint8_t *data, uint32_t *cbData);
    typedef uint32_t (__attribute__((stdcall)) *pfn_RegEnumKeyExA)(
        uint32_t hKey, uint32_t idx, char *name, uint32_t *cchName,
        uint32_t *res, char *cls, uint32_t *cchCls, void *ft);
    typedef uint32_t (__attribute__((stdcall)) *pfn_RegEnumValueA)(
        uint32_t hKey, uint32_t idx, char *name, uint32_t *cchName,
        uint32_t *res, uint32_t *type, uint8_t *data, uint32_t *cbData);
    typedef uint32_t (__attribute__((stdcall)) *pfn_RegCreateKeyExA)(
        uint32_t hKey, const char *sub, uint32_t res, char *cls,
        uint32_t opts, uint32_t sam, void *sa, uint32_t *out, uint32_t *disp);
    typedef uint32_t (__attribute__((stdcall)) *pfn_RegSetValueExA)(
        uint32_t hKey, const char *name, uint32_t res, uint32_t type,
        const uint8_t *data, uint32_t cbData);

    /* Resolve all registry functions from advapi32 shim */
    pfn_RegOpenKeyExA pOpen = (pfn_RegOpenKeyExA)
        win32_resolve_import("advapi32.dll", "RegOpenKeyExA");
    pfn_RegCloseKey pClose = (pfn_RegCloseKey)
        win32_resolve_import("advapi32.dll", "RegCloseKey");
    pfn_RegQueryValueExA pQuery = (pfn_RegQueryValueExA)
        win32_resolve_import("advapi32.dll", "RegQueryValueExA");
    pfn_RegEnumKeyExA pEnumKey = (pfn_RegEnumKeyExA)
        win32_resolve_import("advapi32.dll", "RegEnumKeyExA");
    pfn_RegEnumValueA pEnumVal = (pfn_RegEnumValueA)
        win32_resolve_import("advapi32.dll", "RegEnumValueA");
    pfn_RegCreateKeyExA pCreate = (pfn_RegCreateKeyExA)
        win32_resolve_import("advapi32.dll", "RegCreateKeyExA");
    pfn_RegSetValueExA pSet = (pfn_RegSetValueExA)
        win32_resolve_import("advapi32.dll", "RegSetValueExA");

    TEST_ASSERT(pOpen != NULL,    "reg: RegOpenKeyExA resolved");
    TEST_ASSERT(pClose != NULL,   "reg: RegCloseKey resolved");
    TEST_ASSERT(pQuery != NULL,   "reg: RegQueryValueExA resolved");
    TEST_ASSERT(pEnumKey != NULL, "reg: RegEnumKeyExA resolved");
    TEST_ASSERT(pEnumVal != NULL, "reg: RegEnumValueA resolved");
    TEST_ASSERT(pCreate != NULL,  "reg: RegCreateKeyExA resolved");
    TEST_ASSERT(pSet != NULL,     "reg: RegSetValueExA resolved");

    if (!pOpen || !pClose || !pQuery || !pEnumKey || !pEnumVal || !pCreate || !pSet)
        return;

    #define HKLM 0x80000002
    #define HKCU 0x80000001

    /* Test 1: Open a pre-populated key */
    uint32_t hKey = 0;
    uint32_t ret = pOpen(HKLM, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                         0, 0x20019, &hKey);
    TEST_ASSERT(ret == 0, "reg: open CurrentVersion succeeds");
    TEST_ASSERT(hKey != 0, "reg: open returns non-null handle");

    /* Test 2: Query REG_SZ value "ProductName" */
    if (hKey) {
        uint32_t type = 0;
        uint8_t data[256];
        uint32_t cbData = sizeof(data);
        ret = pQuery(hKey, "ProductName", NULL, &type, data, &cbData);
        TEST_ASSERT(ret == 0, "reg: query ProductName succeeds");
        TEST_ASSERT(type == 1, "reg: ProductName is REG_SZ");
        TEST_ASSERT(strcmp((char *)data, "Windows 10 Pro") == 0, "reg: ProductName value");
    }

    /* Test 3: Query with NULL data returns required size */
    if (hKey) {
        uint32_t type = 0;
        uint32_t cbData = 0;
        ret = pQuery(hKey, "ProductName", NULL, &type, NULL, &cbData);
        TEST_ASSERT(ret == 0, "reg: query NULL data returns size");
        TEST_ASSERT(cbData == strlen("Windows 10 Pro") + 1, "reg: correct size returned");
    }

    /* Test 4: Query with small buffer returns ERROR_MORE_DATA */
    if (hKey) {
        uint8_t small[4];
        uint32_t cbData = sizeof(small);
        ret = pQuery(hKey, "ProductName", NULL, NULL, small, &cbData);
        TEST_ASSERT(ret == 234, "reg: small buffer returns ERROR_MORE_DATA");
    }

    /* Test 5: Query REG_DWORD */
    if (hKey) {
        uint32_t type = 0;
        uint32_t dw_data = 0;
        uint32_t cbData = sizeof(dw_data);
        ret = pQuery(hKey, "CurrentMajorVersionNumber", NULL, &type,
                     (uint8_t *)&dw_data, &cbData);
        TEST_ASSERT(ret == 0, "reg: query DWORD succeeds");
        TEST_ASSERT(type == 4, "reg: DWORD type is REG_DWORD");
        TEST_ASSERT(dw_data == 10, "reg: MajorVersion is 10");
    }

    /* Test 6: Close key */
    if (hKey) {
        ret = pClose(hKey);
        TEST_ASSERT(ret == 0, "reg: close succeeds");
    }

    /* Test 7: Open non-existent key returns ERROR_FILE_NOT_FOUND */
    uint32_t hBogus = 0;
    ret = pOpen(HKLM, "SOFTWARE\\NonExistent\\Key", 0, 0x20019, &hBogus);
    TEST_ASSERT(ret == 2, "reg: open bogus key returns FILE_NOT_FOUND");

    /* Test 8: RegEnumKeyExA on a parent key */
    uint32_t hSoftware = 0;
    ret = pOpen(HKLM, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, 0x20019, &hSoftware);
    if (ret == 0 && hSoftware) {
        /* CurrentVersion has children: Fonts, FontSubstitutes */
        char name[128];
        uint32_t cchName = sizeof(name);
        ret = pEnumKey(hSoftware, 0, name, &cchName, NULL, NULL, NULL, NULL);
        TEST_ASSERT(ret == 0, "reg: enum key index 0 succeeds");

        /* Enum past all children should return ERROR_NO_MORE_ITEMS */
        cchName = sizeof(name);
        uint32_t r2 = pEnumKey(hSoftware, 100, name, &cchName, NULL, NULL, NULL, NULL);
        TEST_ASSERT(r2 == 259, "reg: enum past end returns NO_MORE_ITEMS");

        pClose(hSoftware);
    }

    /* Test 9: RegEnumValueA */
    uint32_t hCodePage = 0;
    ret = pOpen(HKLM, "SYSTEM\\CurrentControlSet\\Control\\Nls\\CodePage",
                0, 0x20019, &hCodePage);
    if (ret == 0 && hCodePage) {
        char vname[64];
        uint32_t cchName = sizeof(vname);
        uint32_t type = 0;
        uint8_t vdata[256];
        uint32_t cbData = sizeof(vdata);
        ret = pEnumVal(hCodePage, 0, vname, &cchName, NULL, &type, vdata, &cbData);
        TEST_ASSERT(ret == 0, "reg: enum value index 0 succeeds");
        TEST_ASSERT(type == 1, "reg: enum value is REG_SZ");

        /* Past all values */
        cchName = sizeof(vname);
        cbData = sizeof(vdata);
        uint32_t r2 = pEnumVal(hCodePage, 100, vname, &cchName, NULL, &type, vdata, &cbData);
        TEST_ASSERT(r2 == 259, "reg: enum value past end returns NO_MORE_ITEMS");

        pClose(hCodePage);
    }

    /* Test 10: RegCreateKeyExA + RegSetValueExA round-trip */
    uint32_t hNew = 0;
    uint32_t disp = 0;
    ret = pCreate(HKCU, "Software\\TestApp\\Settings", 0, NULL, 0, 0xF003F, NULL, &hNew, &disp);
    TEST_ASSERT(ret == 0, "reg: create new key succeeds");
    TEST_ASSERT(disp == 1 || disp == 2, "reg: disposition is CREATED_NEW or OPENED_EXISTING");

    if (hNew) {
        /* Set a string value */
        const char *val = "hello";
        ret = pSet(hNew, "MyValue", 0, 1, (const uint8_t *)val, strlen(val) + 1);
        TEST_ASSERT(ret == 0, "reg: set value succeeds");

        /* Read it back */
        uint32_t type = 0;
        uint8_t readback[64];
        uint32_t cbData = sizeof(readback);
        ret = pQuery(hNew, "MyValue", NULL, &type, readback, &cbData);
        TEST_ASSERT(ret == 0, "reg: query round-trip succeeds");
        TEST_ASSERT(type == 1, "reg: round-trip type is REG_SZ");
        TEST_ASSERT(strcmp((char *)readback, "hello") == 0, "reg: round-trip value matches");

        pClose(hNew);
    }

    /* Test 11: Re-open created key returns OPENED_EXISTING */
    uint32_t hNew2 = 0;
    uint32_t disp2 = 0;
    ret = pCreate(HKCU, "Software\\TestApp\\Settings", 0, NULL, 0, 0xF003F, NULL, &hNew2, &disp2);
    TEST_ASSERT(ret == 0, "reg: reopen existing key succeeds");
    TEST_ASSERT(disp2 == 2, "reg: disposition is OPENED_EXISTING");
    if (hNew2) pClose(hNew2);

    /* Test 12: Close predefined key is a no-op (success) */
    ret = pClose(HKLM);
    TEST_ASSERT(ret == 0, "reg: close predefined key succeeds");

    #undef HKLM
    #undef HKCU
}

/* ---- Winsock Tests ---- */

static void test_winsock(void) {
    printf("== Winsock Tests ==\n");

    /* Resolve Winsock functions via shim tables */
    typedef int      (__attribute__((stdcall)) *pfn_WSAStartup)(uint16_t, void *);
    typedef int      (__attribute__((stdcall)) *pfn_WSACleanup)(void);
    typedef int      (__attribute__((stdcall)) *pfn_WSAGetLastError)(void);
    typedef uint32_t (__attribute__((stdcall)) *pfn_socket)(int, int, int);
    typedef int      (__attribute__((stdcall)) *pfn_closesocket)(uint32_t);
    typedef int      (__attribute__((stdcall)) *pfn_gethostname)(char *, int);
    typedef int      (__attribute__((stdcall)) *pfn_getaddrinfo)(const char *, const char *, const void *, void **);
    typedef void     (__attribute__((stdcall)) *pfn_freeaddrinfo)(void *);
    typedef int      (__attribute__((stdcall)) *pfn_setsockopt)(uint32_t, int, int, const char *, int);
    typedef int      (__attribute__((stdcall)) *pfn_select)(int, void *, void *, void *, const void *);
    typedef uint16_t (__attribute__((stdcall)) *pfn_htons)(uint16_t);
    typedef uint32_t (__attribute__((stdcall)) *pfn_inet_addr)(const char *);

    pfn_WSAStartup     pWSAStartup     = (pfn_WSAStartup)win32_resolve_import("ws2_32.dll", "WSAStartup");
    pfn_WSACleanup     pWSACleanup     = (pfn_WSACleanup)win32_resolve_import("ws2_32.dll", "WSACleanup");
    pfn_WSAGetLastError pWSAGetLastError = (pfn_WSAGetLastError)win32_resolve_import("ws2_32.dll", "WSAGetLastError");
    pfn_socket         pSocket         = (pfn_socket)win32_resolve_import("ws2_32.dll", "socket");
    pfn_closesocket    pClosesocket    = (pfn_closesocket)win32_resolve_import("ws2_32.dll", "closesocket");
    pfn_gethostname    pGethostname    = (pfn_gethostname)win32_resolve_import("ws2_32.dll", "gethostname");
    pfn_getaddrinfo    pGetaddrinfo    = (pfn_getaddrinfo)win32_resolve_import("ws2_32.dll", "getaddrinfo");
    pfn_freeaddrinfo   pFreeaddrinfo   = (pfn_freeaddrinfo)win32_resolve_import("ws2_32.dll", "freeaddrinfo");
    pfn_setsockopt     pSetsockopt     = (pfn_setsockopt)win32_resolve_import("ws2_32.dll", "setsockopt");
    pfn_select         pSelect         = (pfn_select)win32_resolve_import("ws2_32.dll", "select");
    pfn_htons          pHtons          = (pfn_htons)win32_resolve_import("ws2_32.dll", "htons");
    pfn_inet_addr      pInet_addr      = (pfn_inet_addr)win32_resolve_import("ws2_32.dll", "inet_addr");

    TEST_ASSERT(pWSAStartup != NULL,     "ws: WSAStartup resolved");
    TEST_ASSERT(pWSACleanup != NULL,     "ws: WSACleanup resolved");
    TEST_ASSERT(pWSAGetLastError != NULL, "ws: WSAGetLastError resolved");
    TEST_ASSERT(pSocket != NULL,         "ws: socket resolved");
    TEST_ASSERT(pClosesocket != NULL,    "ws: closesocket resolved");
    TEST_ASSERT(pGethostname != NULL,    "ws: gethostname resolved");
    TEST_ASSERT(pGetaddrinfo != NULL,    "ws: getaddrinfo resolved");
    TEST_ASSERT(pFreeaddrinfo != NULL,   "ws: freeaddrinfo resolved");
    TEST_ASSERT(pSetsockopt != NULL,     "ws: setsockopt resolved");
    TEST_ASSERT(pSelect != NULL,         "ws: select resolved");
    TEST_ASSERT(pHtons != NULL,          "ws: htons resolved");
    TEST_ASSERT(pInet_addr != NULL,      "ws: inet_addr resolved");

    if (!pWSAStartup || !pSocket || !pClosesocket) return;

    /* Test 1: WSAStartup returns 0 and fills WSADATA */
    uint8_t wsadata[400];
    memset(wsadata, 0, sizeof(wsadata));
    int ret = pWSAStartup(0x0202, wsadata);
    TEST_ASSERT(ret == 0, "ws: WSAStartup returns 0");
    /* Check version field (first 2 bytes) */
    uint16_t ver = *(uint16_t *)wsadata;
    TEST_ASSERT(ver == 0x0202, "ws: WSADATA version is 2.2");

    /* Test 2: socket(AF_INET, SOCK_STREAM, 0) returns valid handle */
    uint32_t s = pSocket(2, 1, 0);  /* AF_INET=2, SOCK_STREAM=1 */
    TEST_ASSERT(s != ~0u, "ws: socket returns valid handle");
    TEST_ASSERT(s >= 0x100, "ws: socket handle >= SOCK_HANDLE_BASE");

    /* Test 3: closesocket succeeds */
    if (s != ~0u) {
        ret = pClosesocket(s);
        TEST_ASSERT(ret == 0, "ws: closesocket returns 0");
    }

    /* Test 4: gethostname returns non-empty string */
    if (pGethostname) {
        char hostname[64];
        memset(hostname, 0, sizeof(hostname));
        ret = pGethostname(hostname, sizeof(hostname));
        TEST_ASSERT(ret == 0, "ws: gethostname returns 0");
        TEST_ASSERT(strlen(hostname) > 0, "ws: hostname non-empty");
    }

    /* Test 5: getaddrinfo("localhost") / freeaddrinfo round-trip */
    if (pGetaddrinfo && pFreeaddrinfo) {
        void *result = NULL;
        ret = pGetaddrinfo("localhost", NULL, NULL, &result);
        TEST_ASSERT(ret == 0, "ws: getaddrinfo(localhost) returns 0");
        TEST_ASSERT(result != NULL, "ws: getaddrinfo result non-NULL");
        if (result) {
            pFreeaddrinfo(result);
            TEST_ASSERT(1, "ws: freeaddrinfo no crash");
        }
    }

    /* Test 6: setsockopt stub returns 0 */
    if (pSetsockopt) {
        int optval = 1;
        ret = pSetsockopt(0x100, 0xFFFF, 0x0004, (const char *)&optval, sizeof(optval));
        TEST_ASSERT(ret == 0, "ws: setsockopt stub returns 0");
    }

    /* Test 7: select(0, NULL, NULL, NULL, &tv) returns 0 */
    if (pSelect) {
        struct { int32_t tv_sec; int32_t tv_usec; } tv = {0, 0};
        ret = pSelect(0, NULL, NULL, NULL, &tv);
        TEST_ASSERT(ret == 0, "ws: select with NULL sets returns 0");
    }

    /* Test 8: htons/ntohs byte swap */
    if (pHtons) {
        uint16_t swapped = pHtons(0x1234);
        TEST_ASSERT(swapped == 0x3412, "ws: htons byte swap");
    }

    /* Test 9: inet_addr */
    if (pInet_addr) {
        uint32_t addr = pInet_addr("127.0.0.1");
        TEST_ASSERT(addr == 0x0100007F, "ws: inet_addr(127.0.0.1)");
    }

    /* Test 10: WSAGetLastError after clean state */
    if (pWSAGetLastError) {
        int err = pWSAGetLastError();
        TEST_ASSERT(err == 0, "ws: WSAGetLastError returns 0 initially");
    }

    /* Cleanup */
    if (pWSACleanup) pWSACleanup();
}

/* ---- Advanced GDI Tests ---- */

static void test_gdi_advanced(void) {
    printf("== Advanced GDI Tests ==\n");

    /* Resolve GDI functions via shim tables */
    typedef uint32_t (__attribute__((stdcall)) *pfn_CreateCompatibleDC)(uint32_t);
    typedef int      (__attribute__((stdcall)) *pfn_DeleteDC)(uint32_t);
    typedef uint32_t (__attribute__((stdcall)) *pfn_CreateCompatibleBitmap)(uint32_t, int, int);
    typedef uint32_t (__attribute__((stdcall)) *pfn_SelectObject)(uint32_t, uint32_t);
    typedef int      (__attribute__((stdcall)) *pfn_DeleteObject)(uint32_t);
    typedef uint32_t (__attribute__((stdcall)) *pfn_CreatePen)(int, int, uint32_t);
    typedef int      (__attribute__((stdcall)) *pfn_MoveToEx)(uint32_t, int, int, void *);
    typedef int      (__attribute__((stdcall)) *pfn_LineTo)(uint32_t, int, int);
    typedef int      (__attribute__((stdcall)) *pfn_GetTextMetricsA)(uint32_t, void *);
    typedef int      (__attribute__((stdcall)) *pfn_GetTextExtentPoint32A)(uint32_t, const char *, int, void *);
    typedef int      (__attribute__((stdcall)) *pfn_SaveDC)(uint32_t);
    typedef int      (__attribute__((stdcall)) *pfn_RestoreDC)(uint32_t, int);
    typedef int      (__attribute__((stdcall)) *pfn_SetTextColor)(uint32_t, uint32_t);
    typedef int      (__attribute__((stdcall)) *pfn_GetDeviceCaps)(uint32_t, int);
    typedef uint32_t (__attribute__((stdcall)) *pfn_CreateDIBSection)(uint32_t, const void *, uint32_t, void **, uint32_t, uint32_t);
    typedef int      (__attribute__((stdcall)) *pfn_GetObjectA)(uint32_t, int, void *);
    typedef int      (__attribute__((stdcall)) *pfn_StretchBlt)(uint32_t, int, int, int, int, uint32_t, int, int, int, int, uint32_t);
    typedef int      (__attribute__((stdcall)) *pfn_SetViewportOrgEx)(uint32_t, int, int, void *);
    typedef int      (__attribute__((stdcall)) *pfn_IntersectClipRect)(uint32_t, int, int, int, int);
    typedef uint32_t (__attribute__((stdcall)) *pfn_CreateRectRgn)(int, int, int, int);
    typedef int      (__attribute__((stdcall)) *pfn_SelectClipRgn)(uint32_t, uint32_t);
    typedef int      (__attribute__((stdcall)) *pfn_EnumFontFamiliesExA)(uint32_t, void *, void *, uint32_t, uint32_t);

    pfn_CreateCompatibleDC pCreateCompatibleDC = (pfn_CreateCompatibleDC)win32_resolve_import("gdi32.dll", "CreateCompatibleDC");
    pfn_DeleteDC pDeleteDC = (pfn_DeleteDC)win32_resolve_import("gdi32.dll", "DeleteDC");
    pfn_CreateCompatibleBitmap pCreateCompatibleBitmap = (pfn_CreateCompatibleBitmap)win32_resolve_import("gdi32.dll", "CreateCompatibleBitmap");
    pfn_SelectObject pSelectObject = (pfn_SelectObject)win32_resolve_import("gdi32.dll", "SelectObject");
    pfn_DeleteObject pDeleteObject = (pfn_DeleteObject)win32_resolve_import("gdi32.dll", "DeleteObject");
    pfn_CreatePen pCreatePen = (pfn_CreatePen)win32_resolve_import("gdi32.dll", "CreatePen");
    pfn_MoveToEx pMoveToEx = (pfn_MoveToEx)win32_resolve_import("gdi32.dll", "MoveToEx");
    pfn_LineTo pLineTo = (pfn_LineTo)win32_resolve_import("gdi32.dll", "LineTo");
    pfn_GetTextMetricsA pGetTextMetricsA = (pfn_GetTextMetricsA)win32_resolve_import("gdi32.dll", "GetTextMetricsA");
    pfn_GetTextExtentPoint32A pGetTextExtentPoint32A = (pfn_GetTextExtentPoint32A)win32_resolve_import("gdi32.dll", "GetTextExtentPoint32A");
    pfn_SaveDC pSaveDC = (pfn_SaveDC)win32_resolve_import("gdi32.dll", "SaveDC");
    pfn_RestoreDC pRestoreDC = (pfn_RestoreDC)win32_resolve_import("gdi32.dll", "RestoreDC");
    pfn_SetTextColor pSetTextColor = (pfn_SetTextColor)win32_resolve_import("gdi32.dll", "SetTextColor");
    pfn_GetDeviceCaps pGetDeviceCaps = (pfn_GetDeviceCaps)win32_resolve_import("gdi32.dll", "GetDeviceCaps");
    pfn_CreateDIBSection pCreateDIBSection = (pfn_CreateDIBSection)win32_resolve_import("gdi32.dll", "CreateDIBSection");
    pfn_GetObjectA pGetObjectA = (pfn_GetObjectA)win32_resolve_import("gdi32.dll", "GetObjectA");
    pfn_StretchBlt pStretchBlt = (pfn_StretchBlt)win32_resolve_import("gdi32.dll", "StretchBlt");
    pfn_SetViewportOrgEx pSetViewportOrgEx = (pfn_SetViewportOrgEx)win32_resolve_import("gdi32.dll", "SetViewportOrgEx");
    pfn_IntersectClipRect pIntersectClipRect = (pfn_IntersectClipRect)win32_resolve_import("gdi32.dll", "IntersectClipRect");
    pfn_CreateRectRgn pCreateRectRgn = (pfn_CreateRectRgn)win32_resolve_import("gdi32.dll", "CreateRectRgn");
    pfn_SelectClipRgn pSelectClipRgn = (pfn_SelectClipRgn)win32_resolve_import("gdi32.dll", "SelectClipRgn");
    pfn_EnumFontFamiliesExA pEnumFontFamiliesExA = (pfn_EnumFontFamiliesExA)win32_resolve_import("gdi32.dll", "EnumFontFamiliesExA");

    /* Verify all functions resolved */
    TEST_ASSERT(pCreateCompatibleDC != NULL, "gdi: CreateCompatibleDC resolved");
    TEST_ASSERT(pDeleteDC != NULL, "gdi: DeleteDC resolved");
    TEST_ASSERT(pCreateCompatibleBitmap != NULL, "gdi: CreateCompatibleBitmap resolved");
    TEST_ASSERT(pCreatePen != NULL, "gdi: CreatePen resolved");
    TEST_ASSERT(pMoveToEx != NULL, "gdi: MoveToEx resolved");
    TEST_ASSERT(pLineTo != NULL, "gdi: LineTo resolved");
    TEST_ASSERT(pGetTextMetricsA != NULL, "gdi: GetTextMetricsA resolved");
    TEST_ASSERT(pGetTextExtentPoint32A != NULL, "gdi: GetTextExtentPoint32A resolved");
    TEST_ASSERT(pSaveDC != NULL, "gdi: SaveDC resolved");
    TEST_ASSERT(pRestoreDC != NULL, "gdi: RestoreDC resolved");
    TEST_ASSERT(pGetDeviceCaps != NULL, "gdi: GetDeviceCaps resolved");
    TEST_ASSERT(pCreateDIBSection != NULL, "gdi: CreateDIBSection resolved");
    TEST_ASSERT(pGetObjectA != NULL, "gdi: GetObjectA resolved");
    TEST_ASSERT(pStretchBlt != NULL, "gdi: StretchBlt resolved");
    TEST_ASSERT(pEnumFontFamiliesExA != NULL, "gdi: EnumFontFamiliesExA resolved");

    if (!pCreateCompatibleDC || !pDeleteDC || !pCreateCompatibleBitmap) return;

    /* Test 1: CreateCompatibleDC returns valid handle */
    uint32_t memDC = pCreateCompatibleDC(0);
    TEST_ASSERT(memDC != 0, "gdi: CreateCompatibleDC returns non-zero");

    /* Test 2: CreateCompatibleBitmap */
    uint32_t hBmp = pCreateCompatibleBitmap(memDC, 64, 48);
    TEST_ASSERT(hBmp != 0, "gdi: CreateCompatibleBitmap returns non-zero");

    /* Test 3: SelectObject bitmap into memory DC */
    if (hBmp && memDC) {
        pSelectObject(memDC, hBmp);
        TEST_ASSERT(1, "gdi: SelectObject bitmap no crash");
    }

    /* Test 4: GetObjectA on bitmap */
    if (hBmp && pGetObjectA) {
        BITMAP bm;
        memset(&bm, 0, sizeof(bm));
        int ret = pGetObjectA(hBmp, sizeof(BITMAP), &bm);
        TEST_ASSERT(ret == (int)sizeof(BITMAP), "gdi: GetObjectA returns BITMAP size");
        TEST_ASSERT(bm.bmWidth == 64, "gdi: GetObjectA bitmap width 64");
        TEST_ASSERT(bm.bmHeight == 48, "gdi: GetObjectA bitmap height 48");
        TEST_ASSERT(bm.bmBitsPixel == 32, "gdi: GetObjectA bitmap bpp 32");
    }

    /* Test 5: CreateDIBSection */
    if (pCreateDIBSection) {
        BITMAPINFO bmi;
        memset(&bmi, 0, sizeof(bmi));
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = 32;
        bmi.bmiHeader.biHeight = -32;  /* top-down */
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = 0;

        void *bits = NULL;
        uint32_t hDib = pCreateDIBSection(memDC, &bmi, 0, &bits, 0, 0);
        TEST_ASSERT(hDib != 0, "gdi: CreateDIBSection returns non-zero");
        TEST_ASSERT(bits != NULL, "gdi: CreateDIBSection sets bits pointer");

        /* Write a pixel to verify memory */
        if (bits) {
            ((uint32_t *)bits)[0] = 0xFF0000;
            TEST_ASSERT(((uint32_t *)bits)[0] == 0xFF0000, "gdi: DIBSection write/read");
        }

        if (hDib) pDeleteObject(hDib);
    }

    /* Test 6: CreatePen */
    if (pCreatePen) {
        uint32_t hPen = pCreatePen(0, 1, RGB(255, 0, 0));
        TEST_ASSERT(hPen != 0, "gdi: CreatePen returns non-zero");
        if (hPen) pDeleteObject(hPen);
    }

    /* Test 7: MoveToEx / LineTo */
    if (pMoveToEx && pLineTo && memDC) {
        int ret = pMoveToEx(memDC, 0, 0, NULL);
        TEST_ASSERT(ret != 0, "gdi: MoveToEx returns TRUE");
        ret = pLineTo(memDC, 10, 10);
        TEST_ASSERT(ret != 0, "gdi: LineTo returns TRUE");
    }

    /* Test 8: GetTextMetricsA */
    if (pGetTextMetricsA && memDC) {
        TEXTMETRICA tm;
        memset(&tm, 0, sizeof(tm));
        int ret = pGetTextMetricsA(memDC, &tm);
        TEST_ASSERT(ret != 0, "gdi: GetTextMetricsA returns TRUE");
        TEST_ASSERT(tm.tmHeight == 16, "gdi: TextMetrics height 16");
        TEST_ASSERT(tm.tmAveCharWidth == 8, "gdi: TextMetrics aveCharWidth 8");
    }

    /* Test 9: GetTextExtentPoint32A */
    if (pGetTextExtentPoint32A && memDC) {
        SIZE sz;
        memset(&sz, 0, sizeof(sz));
        int ret = pGetTextExtentPoint32A(memDC, "Hello", 5, &sz);
        TEST_ASSERT(ret != 0, "gdi: GetTextExtentPoint32A returns TRUE");
        TEST_ASSERT(sz.cx == 40, "gdi: text extent cx = 5*8 = 40");
        TEST_ASSERT(sz.cy == 16, "gdi: text extent cy = 16");
    }

    /* Test 10: SaveDC / RestoreDC */
    if (pSaveDC && pRestoreDC && pSetTextColor && memDC) {
        pSetTextColor(memDC, RGB(255, 0, 0));
        int level = pSaveDC(memDC);
        TEST_ASSERT(level > 0, "gdi: SaveDC returns positive level");

        pSetTextColor(memDC, RGB(0, 255, 0));

        int ret = pRestoreDC(memDC, -1);
        TEST_ASSERT(ret != 0, "gdi: RestoreDC returns TRUE");
    }

    /* Test 11: GetDeviceCaps */
    if (pGetDeviceCaps) {
        int hres = pGetDeviceCaps(memDC, 8);  /* HORZRES */
        int vres = pGetDeviceCaps(memDC, 10); /* VERTRES */
        int bpp  = pGetDeviceCaps(memDC, 12); /* BITSPIXEL */
        int dpi  = pGetDeviceCaps(memDC, 88); /* LOGPIXELSX */
        TEST_ASSERT(hres == 1920, "gdi: GetDeviceCaps HORZRES 1920");
        TEST_ASSERT(vres == 1080, "gdi: GetDeviceCaps VERTRES 1080");
        TEST_ASSERT(bpp == 32, "gdi: GetDeviceCaps BITSPIXEL 32");
        TEST_ASSERT(dpi == 96, "gdi: GetDeviceCaps LOGPIXELSX 96");
    }

    /* Test 12: StretchBlt between memory DCs */
    if (pStretchBlt && pCreateCompatibleDC && pCreateCompatibleBitmap) {
        uint32_t srcDC = pCreateCompatibleDC(0);
        uint32_t srcBmp = pCreateCompatibleBitmap(srcDC, 16, 16);
        pSelectObject(srcDC, srcBmp);

        uint32_t dstDC = pCreateCompatibleDC(0);
        uint32_t dstBmp = pCreateCompatibleBitmap(dstDC, 32, 32);
        pSelectObject(dstDC, dstBmp);

        int ret = pStretchBlt(dstDC, 0, 0, 32, 32, srcDC, 0, 0, 16, 16, 0x00CC0020);
        TEST_ASSERT(ret != 0, "gdi: StretchBlt returns TRUE");

        pDeleteObject(srcBmp);
        pDeleteDC(srcDC);
        pDeleteObject(dstBmp);
        pDeleteDC(dstDC);
    }

    /* Test 13: SetViewportOrgEx */
    if (pSetViewportOrgEx && memDC) {
        POINT oldOrg;
        int ret = pSetViewportOrgEx(memDC, 10, 20, &oldOrg);
        TEST_ASSERT(ret != 0, "gdi: SetViewportOrgEx returns TRUE");
        TEST_ASSERT(oldOrg.x == 0 && oldOrg.y == 0, "gdi: old viewport origin (0,0)");
    }

    /* Test 14: IntersectClipRect */
    if (pIntersectClipRect && memDC) {
        int ret = pIntersectClipRect(memDC, 10, 10, 100, 100);
        TEST_ASSERT(ret == 2, "gdi: IntersectClipRect returns SIMPLEREGION");
    }

    /* Test 15: CreateRectRgn / SelectClipRgn */
    if (pCreateRectRgn && pSelectClipRgn && memDC) {
        uint32_t hRgn = pCreateRectRgn(0, 0, 50, 50);
        TEST_ASSERT(hRgn != 0, "gdi: CreateRectRgn returns non-zero");
        int ret = pSelectClipRgn(memDC, hRgn);
        TEST_ASSERT(ret == 2, "gdi: SelectClipRgn returns SIMPLEREGION");
        /* Reset clip */
        pSelectClipRgn(memDC, 0);
        pDeleteObject(hRgn);
    }

    /* Test 16: EnumFontFamiliesExA callback */
    if (pEnumFontFamiliesExA && memDC) {
        /* We'll just check it calls back — the callback is stdcall but we test via
         * the function returning a value (the callback return value) */
        int ret = pEnumFontFamiliesExA(memDC, NULL, NULL, 0, 0);
        /* NULL proc returns 0 */
        TEST_ASSERT(ret == 0, "gdi: EnumFontFamiliesExA NULL proc returns 0");
    }

    /* Cleanup */
    if (hBmp) pDeleteObject(hBmp);
    if (memDC) pDeleteDC(memDC);

    /* === GDI+ Tests === */
    printf("== GDI+ Tests ==\n");

    typedef int      (__attribute__((stdcall)) *pfn_GdiplusStartup)(uint32_t *, const void *, void *);
    typedef void     (__attribute__((stdcall)) *pfn_GdiplusShutdown)(uint32_t);
    typedef int      (__attribute__((stdcall)) *pfn_GdipCreateFromHDC)(uint32_t, uint32_t *);
    typedef int      (__attribute__((stdcall)) *pfn_GdipDeleteGraphics)(uint32_t);

    pfn_GdiplusStartup pGdiplusStartup = (pfn_GdiplusStartup)win32_resolve_import("gdiplus.dll", "GdiplusStartup");
    pfn_GdiplusShutdown pGdiplusShutdown = (pfn_GdiplusShutdown)win32_resolve_import("gdiplus.dll", "GdiplusShutdown");
    pfn_GdipCreateFromHDC pGdipCreateFromHDC = (pfn_GdipCreateFromHDC)win32_resolve_import("gdiplus.dll", "GdipCreateFromHDC");
    pfn_GdipDeleteGraphics pGdipDeleteGraphics = (pfn_GdipDeleteGraphics)win32_resolve_import("gdiplus.dll", "GdipDeleteGraphics");

    TEST_ASSERT(pGdiplusStartup != NULL, "gdip: GdiplusStartup resolved");
    TEST_ASSERT(pGdiplusShutdown != NULL, "gdip: GdiplusShutdown resolved");
    TEST_ASSERT(pGdipCreateFromHDC != NULL, "gdip: GdipCreateFromHDC resolved");
    TEST_ASSERT(pGdipDeleteGraphics != NULL, "gdip: GdipDeleteGraphics resolved");

    if (pGdiplusStartup && pGdiplusShutdown) {
        uint32_t token = 0;
        struct { uint32_t ver; void *cb; int a; int b; } input = {1, NULL, 0, 0};
        int ret = pGdiplusStartup(&token, &input, NULL);
        TEST_ASSERT(ret == 0, "gdip: GdiplusStartup returns Ok");
        TEST_ASSERT(token != 0, "gdip: GdiplusStartup sets token");

        if (pGdipCreateFromHDC && pGdipDeleteGraphics) {
            uint32_t graphics = 0;
            ret = pGdipCreateFromHDC(1, &graphics);
            TEST_ASSERT(ret == 0, "gdip: GdipCreateFromHDC returns Ok");
            TEST_ASSERT(graphics != 0, "gdip: GdipCreateFromHDC sets handle");

            ret = pGdipDeleteGraphics(graphics);
            TEST_ASSERT(ret == 0, "gdip: GdipDeleteGraphics returns Ok");
        }

        pGdiplusShutdown(token);
        TEST_ASSERT(1, "gdip: GdiplusShutdown no crash");
    }
}

/* ---- COM & OLE Tests ---- */

static void test_com_ole(void) {
    printf("== COM & OLE Tests ==\n");

    /* Resolve COM functions */
    typedef HRESULT (WINAPI *pfn_CoInitializeEx)(LPVOID, DWORD);
    typedef void    (WINAPI *pfn_CoUninitialize)(void);
    typedef LPVOID  (WINAPI *pfn_CoTaskMemAlloc)(DWORD);
    typedef void    (WINAPI *pfn_CoTaskMemFree)(LPVOID);
    typedef HRESULT (WINAPI *pfn_CoCreateInstance)(REFCLSID, LPVOID, DWORD, REFIID, LPVOID *);
    typedef HRESULT (WINAPI *pfn_CoGetMalloc)(DWORD, void **);
    typedef HRESULT (WINAPI *pfn_OleInitialize)(LPVOID);
    typedef void    (WINAPI *pfn_OleUninitialize)(void);
    typedef int     (WINAPI *pfn_StringFromGUID2)(const GUID *, WCHAR *, int);

    pfn_CoInitializeEx pCoInitializeEx = (pfn_CoInitializeEx)
        win32_resolve_import("ole32.dll", "CoInitializeEx");
    pfn_CoUninitialize pCoUninitialize = (pfn_CoUninitialize)
        win32_resolve_import("ole32.dll", "CoUninitialize");
    pfn_CoTaskMemAlloc pCoTaskMemAlloc = (pfn_CoTaskMemAlloc)
        win32_resolve_import("ole32.dll", "CoTaskMemAlloc");
    pfn_CoTaskMemFree pCoTaskMemFree = (pfn_CoTaskMemFree)
        win32_resolve_import("ole32.dll", "CoTaskMemFree");
    pfn_CoCreateInstance pCoCreateInstance = (pfn_CoCreateInstance)
        win32_resolve_import("ole32.dll", "CoCreateInstance");
    pfn_CoGetMalloc pCoGetMalloc = (pfn_CoGetMalloc)
        win32_resolve_import("ole32.dll", "CoGetMalloc");
    pfn_OleInitialize pOleInitialize = (pfn_OleInitialize)
        win32_resolve_import("ole32.dll", "OleInitialize");
    pfn_OleUninitialize pOleUninitialize = (pfn_OleUninitialize)
        win32_resolve_import("ole32.dll", "OleUninitialize");
    pfn_StringFromGUID2 pStringFromGUID2 = (pfn_StringFromGUID2)
        win32_resolve_import("ole32.dll", "StringFromGUID2");

    TEST_ASSERT(pCoInitializeEx != NULL, "ole32: CoInitializeEx resolved");
    TEST_ASSERT(pCoUninitialize != NULL, "ole32: CoUninitialize resolved");
    TEST_ASSERT(pCoTaskMemAlloc != NULL, "ole32: CoTaskMemAlloc resolved");
    TEST_ASSERT(pCoTaskMemFree != NULL, "ole32: CoTaskMemFree resolved");
    TEST_ASSERT(pCoCreateInstance != NULL, "ole32: CoCreateInstance resolved");
    TEST_ASSERT(pCoGetMalloc != NULL, "ole32: CoGetMalloc resolved");
    TEST_ASSERT(pOleInitialize != NULL, "ole32: OleInitialize resolved");
    TEST_ASSERT(pStringFromGUID2 != NULL, "ole32: StringFromGUID2 resolved");

    /* Test CoInitializeEx */
    HRESULT hr = pCoInitializeEx(NULL, 0);
    TEST_ASSERT(hr == S_OK, "ole32: CoInitializeEx returns S_OK");

    /* Test CoTaskMemAlloc / Free */
    void *mem = pCoTaskMemAlloc(128);
    TEST_ASSERT(mem != NULL, "ole32: CoTaskMemAlloc(128) not NULL");
    memset(mem, 0xAB, 128);
    pCoTaskMemFree(mem);
    TEST_ASSERT(1, "ole32: CoTaskMemFree no crash");

    /* Test CoGetMalloc / IMalloc */
    void *pMalloc = NULL;
    hr = pCoGetMalloc(1, &pMalloc);
    TEST_ASSERT(hr == S_OK, "ole32: CoGetMalloc returns S_OK");
    TEST_ASSERT(pMalloc != NULL, "ole32: CoGetMalloc returns IMalloc ptr");

    /* Test CoCreateInstance — should fail (no objects registered) */
    CLSID clsid = {0};
    IID iid = {0};
    void *pObj = NULL;
    hr = pCoCreateInstance(&clsid, NULL, 0, &iid, &pObj);
    TEST_ASSERT(hr == REGDB_E_CLASSNOTREG, "ole32: CoCreateInstance returns REGDB_E_CLASSNOTREG");
    TEST_ASSERT(pObj == NULL, "ole32: CoCreateInstance ppv is NULL");

    /* Test StringFromGUID2 */
    GUID test_guid = {0x12345678, 0xABCD, 0xEF01, {0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0x01}};
    WCHAR guid_str[40] = {0};
    int guid_len = pStringFromGUID2(&test_guid, guid_str, 40);
    TEST_ASSERT(guid_len == 39, "ole32: StringFromGUID2 returns 39");
    TEST_ASSERT(guid_str[0] == '{', "ole32: StringFromGUID2 starts with '{'");
    TEST_ASSERT(guid_str[37] == '}', "ole32: StringFromGUID2 ends with '}'");

    /* Test OleInitialize / OleUninitialize */
    hr = pOleInitialize(NULL);
    TEST_ASSERT(hr == S_OK, "ole32: OleInitialize returns S_OK");
    pOleUninitialize();
    TEST_ASSERT(1, "ole32: OleUninitialize no crash");

    pCoUninitialize();

    /* Test shell32 */
    typedef HRESULT (WINAPI *pfn_SHGetFolderPathA)(HWND, int, HANDLE, DWORD, LPSTR);
    pfn_SHGetFolderPathA pSHGetFolderPathA = (pfn_SHGetFolderPathA)
        win32_resolve_import("shell32.dll", "SHGetFolderPathA");
    TEST_ASSERT(pSHGetFolderPathA != NULL, "shell32: SHGetFolderPathA resolved");

    if (pSHGetFolderPathA) {
        char path[260];
        hr = pSHGetFolderPathA(0, CSIDL_DESKTOP, 0, 0, path);
        TEST_ASSERT(hr == S_OK, "shell32: SHGetFolderPathA(DESKTOP) returns S_OK");
        TEST_ASSERT(strcmp(path, "/home/user/Desktop") == 0, "shell32: CSIDL_DESKTOP path correct");

        hr = pSHGetFolderPathA(0, CSIDL_APPDATA, 0, 0, path);
        TEST_ASSERT(hr == S_OK, "shell32: SHGetFolderPathA(APPDATA) returns S_OK");
        TEST_ASSERT(strcmp(path, "/home/user/AppData/Roaming") == 0, "shell32: CSIDL_APPDATA path correct");

        hr = pSHGetFolderPathA(0, CSIDL_SYSTEM, 0, 0, path);
        TEST_ASSERT(hr == S_OK, "shell32: SHGetFolderPathA(SYSTEM) returns S_OK");
        TEST_ASSERT(strcmp(path, "C:\\Windows\\System32") == 0, "shell32: CSIDL_SYSTEM path correct");
    }
}

/* ---- Unicode & Wide String Tests ---- */

static void test_unicode_wide(void) {
    printf("== Unicode & Wide String Tests ==\n");

    /* Test proper UTF-8 → UTF-16 conversion */

    /* ASCII */
    WCHAR wbuf[64];
    int n = win32_utf8_to_wchar("Hello", -1, wbuf, 64);
    TEST_ASSERT(n == 6, "utf8→16: ASCII 'Hello' returns 6 (5+null)");
    TEST_ASSERT(wbuf[0] == 'H', "utf8→16: wbuf[0]='H'");
    TEST_ASSERT(wbuf[4] == 'o', "utf8→16: wbuf[4]='o'");
    TEST_ASSERT(wbuf[5] == 0, "utf8→16: wbuf[5]=null");

    /* 2-byte UTF-8 (é = U+00E9 = 0xC3 0xA9) */
    n = win32_utf8_to_wchar("\xC3\xA9", -1, wbuf, 64);
    TEST_ASSERT(n == 2, "utf8→16: 2-byte é returns 2 (1+null)");
    TEST_ASSERT(wbuf[0] == 0x00E9, "utf8→16: é = U+00E9");

    /* 3-byte UTF-8 (€ = U+20AC = 0xE2 0x82 0xAC) */
    n = win32_utf8_to_wchar("\xE2\x82\xAC", -1, wbuf, 64);
    TEST_ASSERT(n == 2, "utf8→16: 3-byte € returns 2 (1+null)");
    TEST_ASSERT(wbuf[0] == 0x20AC, "utf8→16: € = U+20AC");

    /* Test UTF-16 → UTF-8 reverse */
    char nbuf[64];
    WCHAR wsrc[] = {'H', 'i', 0x00E9, 0};
    n = win32_wchar_to_utf8(wsrc, -1, nbuf, 64);
    TEST_ASSERT(n == 5, "utf16→8: 'Hié' returns 5 (H+i+2byte+null)");
    TEST_ASSERT(nbuf[0] == 'H', "utf16→8: nbuf[0]='H'");
    TEST_ASSERT(nbuf[1] == 'i', "utf16→8: nbuf[1]='i'");
    TEST_ASSERT((uint8_t)nbuf[2] == 0xC3, "utf16→8: é byte1=0xC3");
    TEST_ASSERT((uint8_t)nbuf[3] == 0xA9, "utf16→8: é byte2=0xA9");

    /* Test wcslen */
    typedef size_t (*pfn_wcslen)(const WCHAR *);
    pfn_wcslen pWcslen = (pfn_wcslen)win32_resolve_import("msvcrt.dll", "wcslen");
    TEST_ASSERT(pWcslen != NULL, "msvcrt: wcslen resolved");
    if (pWcslen) {
        WCHAR test[] = {'A', 'B', 'C', 0};
        TEST_ASSERT(pWcslen(test) == 3, "wcslen: 'ABC' = 3");
    }

    /* Test wcscpy */
    typedef WCHAR *(*pfn_wcscpy)(WCHAR *, const WCHAR *);
    pfn_wcscpy pWcscpy = (pfn_wcscpy)win32_resolve_import("msvcrt.dll", "wcscpy");
    TEST_ASSERT(pWcscpy != NULL, "msvcrt: wcscpy resolved");
    if (pWcscpy) {
        WCHAR src[] = {'X', 'Y', 0};
        WCHAR dst[8] = {0};
        pWcscpy(dst, src);
        TEST_ASSERT(dst[0] == 'X' && dst[1] == 'Y' && dst[2] == 0, "wcscpy: copies correctly");
    }

    /* Test wcscmp */
    typedef int (*pfn_wcscmp)(const WCHAR *, const WCHAR *);
    pfn_wcscmp pWcscmp = (pfn_wcscmp)win32_resolve_import("msvcrt.dll", "wcscmp");
    TEST_ASSERT(pWcscmp != NULL, "msvcrt: wcscmp resolved");
    if (pWcscmp) {
        WCHAR a[] = {'A', 'B', 0};
        WCHAR b[] = {'A', 'B', 0};
        WCHAR c[] = {'A', 'C', 0};
        TEST_ASSERT(pWcscmp(a, b) == 0, "wcscmp: equal strings return 0");
        TEST_ASSERT(pWcscmp(a, c) < 0, "wcscmp: 'AB' < 'AC'");
    }

    /* Test W function resolution */
    TEST_ASSERT(win32_resolve_import("kernel32.dll", "CreateFileW") != NULL,
        "resolve: CreateFileW found");
    TEST_ASSERT(win32_resolve_import("user32.dll", "MessageBoxW") != NULL,
        "resolve: MessageBoxW found");
    TEST_ASSERT(win32_resolve_import("gdi32.dll", "TextOutW") != NULL,
        "resolve: TextOutW found");
    TEST_ASSERT(win32_resolve_import("msvcrt.dll", "wcslen") != NULL,
        "resolve: wcslen found");
    TEST_ASSERT(win32_resolve_import("ole32.dll", "CoInitializeEx") != NULL,
        "resolve: CoInitializeEx found");
    TEST_ASSERT(win32_resolve_import("shell32.dll", "SHGetFolderPathA") != NULL,
        "resolve: SHGetFolderPathA found");
}

/* ---- Unicode & Internationalization Phase 3 Tests ---- */

static void test_unicode_i18n(void) {
    printf("== Unicode & i18n Phase 3 Tests ==\n");

    /* ── MultiByteToWideChar with code pages ──────────────── */

    typedef int (WINAPI *pfn_MBTWC)(UINT, DWORD, LPCSTR, int, void *, int);
    typedef int (WINAPI *pfn_WCTMB)(UINT, DWORD, const void *, int, LPSTR, int, LPCSTR, LPVOID);
    pfn_MBTWC pMBTWC = (pfn_MBTWC)win32_resolve_import("kernel32.dll", "MultiByteToWideChar");
    pfn_WCTMB pWCTMB = (pfn_WCTMB)win32_resolve_import("kernel32.dll", "WideCharToMultiByte");

    TEST_ASSERT(pMBTWC != NULL, "i18n: MultiByteToWideChar resolved");
    TEST_ASSERT(pWCTMB != NULL, "i18n: WideCharToMultiByte resolved");

    if (pMBTWC) {
        WCHAR wbuf[16];

        /* CP1252: byte 0x80 → U+20AC (Euro sign) */
        char cp1252_euro = (char)0x80;
        int n = pMBTWC(1252, 0, &cp1252_euro, 1, wbuf, 16);
        TEST_ASSERT(n == 1, "i18n: MBTWC CP1252 0x80 count=1");
        TEST_ASSERT(wbuf[0] == 0x20AC, "i18n: MBTWC CP1252 0x80→U+20AC");

        /* CP437: byte 0x80 → U+00C7 (Ç) */
        char cp437_c_cedilla = (char)0x80;
        n = pMBTWC(437, 0, &cp437_c_cedilla, 1, wbuf, 16);
        TEST_ASSERT(n == 1, "i18n: MBTWC CP437 0x80 count=1");
        TEST_ASSERT(wbuf[0] == 0x00C7, "i18n: MBTWC CP437 0x80→U+00C7");

        /* CP_ACP (0) should resolve to CP1252 */
        n = pMBTWC(0, 0, &cp1252_euro, 1, wbuf, 16);
        TEST_ASSERT(wbuf[0] == 0x20AC, "i18n: MBTWC CP_ACP(0)→CP1252");
    }

    if (pWCTMB) {
        char nbuf[16];

        /* U+00E9 → 0xE9 in CP1252 */
        WCHAR e_acute = 0x00E9;
        int n = pWCTMB(1252, 0, &e_acute, 1, nbuf, 16, NULL, NULL);
        TEST_ASSERT(n == 1, "i18n: WCTMB CP1252 U+00E9 count=1");
        TEST_ASSERT((uint8_t)nbuf[0] == 0xE9, "i18n: WCTMB CP1252 U+00E9→0xE9");

        /* U+20AC → 0x80 in CP1252 */
        WCHAR euro = 0x20AC;
        n = pWCTMB(1252, 0, &euro, 1, nbuf, 16, NULL, NULL);
        TEST_ASSERT(n == 1, "i18n: WCTMB CP1252 U+20AC count=1");
        TEST_ASSERT((uint8_t)nbuf[0] == 0x80, "i18n: WCTMB CP1252 U+20AC→0x80");
    }

    /* ── CharUpperW / CharLowerW / IsCharAlphaW ───────────── */

    typedef LPWSTR (WINAPI *pfn_CharUpperW)(LPWSTR);
    typedef BOOL (WINAPI *pfn_IsCharAlphaW)(WCHAR);
    pfn_CharUpperW pCharUpperW = (pfn_CharUpperW)win32_resolve_import("user32.dll", "CharUpperW");
    pfn_IsCharAlphaW pIsCharAlphaW = (pfn_IsCharAlphaW)win32_resolve_import("user32.dll", "IsCharAlphaW");

    TEST_ASSERT(pCharUpperW != NULL, "i18n: CharUpperW resolved");
    if (pCharUpperW) {
        /* Single char mode: low word = char, high word = 0 */
        LPWSTR r = pCharUpperW((LPWSTR)(uintptr_t)(WCHAR)'a');
        TEST_ASSERT((WCHAR)(uintptr_t)r == 'A', "i18n: CharUpperW 'a'→'A'");

        /* Latin-1: U+00E9 (é) → U+00C9 (É) */
        r = pCharUpperW((LPWSTR)(uintptr_t)(WCHAR)0x00E9);
        TEST_ASSERT((WCHAR)(uintptr_t)r == 0x00C9, "i18n: CharUpperW U+00E9→U+00C9");
    }

    TEST_ASSERT(pIsCharAlphaW != NULL, "i18n: IsCharAlphaW resolved");
    if (pIsCharAlphaW) {
        TEST_ASSERT(pIsCharAlphaW('A') == TRUE, "i18n: IsCharAlphaW('A')=true");
        TEST_ASSERT(pIsCharAlphaW('1') == FALSE, "i18n: IsCharAlphaW('1')=false");
        TEST_ASSERT(pIsCharAlphaW(0x00E9) == TRUE, "i18n: IsCharAlphaW(U+00E9)=true");
    }

    /* ── CompareStringW ───────────────────────────────────── */

    typedef int (WINAPI *pfn_CompareStringW)(DWORD, DWORD, const WCHAR *, int, const WCHAR *, int);
    pfn_CompareStringW pCompareStringW = (pfn_CompareStringW)win32_resolve_import("kernel32.dll", "CompareStringW");
    TEST_ASSERT(pCompareStringW != NULL, "i18n: CompareStringW resolved");

    if (pCompareStringW) {
        WCHAR hello_upper[] = {'H','E','L','L','O',0};
        WCHAR hello_lower[] = {'h','e','l','l','o',0};
        /* NORM_IGNORECASE = 1 */
        int r = pCompareStringW(0, 1, hello_upper, -1, hello_lower, -1);
        TEST_ASSERT(r == 2, "i18n: CompareStringW HELLO==hello (ignorecase)");

        /* Case-sensitive should differ */
        r = pCompareStringW(0, 0, hello_upper, -1, hello_lower, -1);
        TEST_ASSERT(r != 2, "i18n: CompareStringW HELLO!=hello (case-sensitive)");
    }

    /* ── wsprintfW ────────────────────────────────────────── */

    typedef int (WINAPI *pfn_wsprintfW)(LPWSTR, LPCWSTR, ...);
    pfn_wsprintfW pWsprintfW = (pfn_wsprintfW)win32_resolve_import("user32.dll", "wsprintfW");
    TEST_ASSERT(pWsprintfW != NULL, "i18n: wsprintfW resolved");

    if (pWsprintfW) {
        WCHAR wbuf[64];
        WCHAR fmt_d[] = {'%','d',0};
        int n = pWsprintfW(wbuf, fmt_d, 42);
        TEST_ASSERT(n == 2, "i18n: wsprintfW %%d returns 2");
        TEST_ASSERT(wbuf[0] == '4' && wbuf[1] == '2', "i18n: wsprintfW %%d='42'");

        WCHAR fmt_s[] = {'%','s',0};
        WCHAR world[] = {'W','o','r','l','d',0};
        n = pWsprintfW(wbuf, fmt_s, world);
        TEST_ASSERT(n == 5, "i18n: wsprintfW %%s returns 5");
        TEST_ASSERT(wbuf[0] == 'W', "i18n: wsprintfW %%s='World'");
    }

    /* ── Console CP ───────────────────────────────────────── */

    typedef BOOL (WINAPI *pfn_SetConsoleCP)(UINT);
    typedef UINT (WINAPI *pfn_GetConsoleCP)(void);
    pfn_SetConsoleCP pSetConsoleCP = (pfn_SetConsoleCP)win32_resolve_import("kernel32.dll", "SetConsoleCP");
    pfn_GetConsoleCP pGetConsoleCP = (pfn_GetConsoleCP)win32_resolve_import("kernel32.dll", "GetConsoleCP");

    TEST_ASSERT(pSetConsoleCP != NULL, "i18n: SetConsoleCP resolved");
    TEST_ASSERT(pGetConsoleCP != NULL, "i18n: GetConsoleCP resolved");
    if (pSetConsoleCP && pGetConsoleCP) {
        pSetConsoleCP(65001);
        TEST_ASSERT(pGetConsoleCP() == 65001, "i18n: console CP round-trip 65001");
        pSetConsoleCP(437); /* restore */
    }

    /* ── msvcrt wide additions ────────────────────────────── */

    typedef long (*pfn_wcstol)(const WCHAR *, WCHAR **, int);
    pfn_wcstol pWcstol = (pfn_wcstol)win32_resolve_import("msvcrt.dll", "wcstol");
    TEST_ASSERT(pWcstol != NULL, "i18n: wcstol resolved");
    if (pWcstol) {
        WCHAR num[] = {'1','2','3',0};
        long v = pWcstol(num, NULL, 10);
        TEST_ASSERT(v == 123, "i18n: wcstol('123')=123");
    }

    typedef int (*pfn_wcsicmp)(const WCHAR *, const WCHAR *);
    pfn_wcsicmp pWcsicmp = (pfn_wcsicmp)win32_resolve_import("msvcrt.dll", "_wcsicmp");
    TEST_ASSERT(pWcsicmp != NULL, "i18n: _wcsicmp resolved");
    if (pWcsicmp) {
        WCHAR a[] = {'H','e','L','L','o',0};
        WCHAR b[] = {'h','E','l','l','O',0};
        TEST_ASSERT(pWcsicmp(a, b) == 0, "i18n: _wcsicmp case-insensitive eq");
    }

    typedef int (*pfn_iswdigit)(WCHAR);
    pfn_iswdigit pIswdigit = (pfn_iswdigit)win32_resolve_import("msvcrt.dll", "iswdigit");
    TEST_ASSERT(pIswdigit != NULL, "i18n: iswdigit resolved");
    if (pIswdigit) {
        TEST_ASSERT(pIswdigit('5') != 0, "i18n: iswdigit('5')=true");
        TEST_ASSERT(pIswdigit('A') == 0, "i18n: iswdigit('A')=false");
    }

    typedef WCHAR (*pfn_towupper)(WCHAR);
    pfn_towupper pTowupper = (pfn_towupper)win32_resolve_import("msvcrt.dll", "towupper");
    TEST_ASSERT(pTowupper != NULL, "i18n: towupper resolved");
    if (pTowupper) {
        TEST_ASSERT(pTowupper('a') == 'A', "i18n: towupper('a')='A'");
        /* Latin-1: U+00E9 → U+00C9 */
        TEST_ASSERT(pTowupper(0x00E9) == 0x00C9, "i18n: towupper(U+00E9)=U+00C9");
    }

    /* ── NLS stubs ────────────────────────────────────────── */

    typedef WORD (WINAPI *pfn_GetLangID)(void);
    pfn_GetLangID pGetUserDefaultLangID = (pfn_GetLangID)win32_resolve_import("kernel32.dll", "GetUserDefaultLangID");
    TEST_ASSERT(pGetUserDefaultLangID != NULL, "i18n: GetUserDefaultLangID resolved");
    if (pGetUserDefaultLangID) {
        TEST_ASSERT(pGetUserDefaultLangID() == 0x0409, "i18n: GetUserDefaultLangID=0x0409");
    }

    typedef int (WINAPI *pfn_GetDateFmt)(DWORD, DWORD, const void *, const WCHAR *, WCHAR *, int);
    pfn_GetDateFmt pGetDateFormatW = (pfn_GetDateFmt)win32_resolve_import("kernel32.dll", "GetDateFormatW");
    TEST_ASSERT(pGetDateFormatW != NULL, "i18n: GetDateFormatW resolved");
    if (pGetDateFormatW) {
        /* Query required size */
        int n = pGetDateFormatW(0, 0, NULL, NULL, NULL, 0);
        TEST_ASSERT(n > 0, "i18n: GetDateFormatW returns >0 for size query");
    }
}

/* ---- Security & Crypto Tests ---- */

static void test_seh(void) {
    printf("== SEH Tests ==\n");

    /* Test 1: SEH types have correct sizes */
    TEST_ASSERT(sizeof(NT_TIB) == 28, "seh: NT_TIB size is 28");
    TEST_ASSERT(sizeof(WIN32_TEB) == 4096, "seh: WIN32_TEB size is 4096");
    TEST_ASSERT(sizeof(EXCEPTION_RECORD) >= 20, "seh: EXCEPTION_RECORD has min size");
    TEST_ASSERT(sizeof(CONTEXT) >= 64, "seh: CONTEXT has min size");
    TEST_ASSERT(sizeof(EXCEPTION_POINTERS) == 8, "seh: EXCEPTION_POINTERS is 8 bytes");

    /* Test 2: NT_TIB field offsets */
    WIN32_TEB teb;
    memset(&teb, 0, sizeof(teb));
    uint8_t *base = (uint8_t *)&teb;
    TEST_ASSERT((uint8_t *)&teb.tib.ExceptionList - base == 0,
                "seh: ExceptionList at offset 0");
    TEST_ASSERT((uint8_t *)&teb.tib.StackBase - base == 4,
                "seh: StackBase at offset 4");
    TEST_ASSERT((uint8_t *)&teb.tib.StackLimit - base == 8,
                "seh: StackLimit at offset 8");
    TEST_ASSERT((uint8_t *)&teb.tib.Self - base == 0x18,
                "seh: Self at offset 0x18");

    /* Test 3: kernel32 SEH exports resolve */
    typedef LPTOP_LEVEL_EXCEPTION_FILTER (WINAPI *pfn_SetUEF)(LPTOP_LEVEL_EXCEPTION_FILTER);
    typedef LONG (WINAPI *pfn_UEF)(EXCEPTION_POINTERS *);
    typedef void (WINAPI *pfn_RaiseException)(DWORD, DWORD, DWORD, const DWORD *);
    typedef void (WINAPI *pfn_RtlUnwind)(void *, void *, EXCEPTION_RECORD *, DWORD);

    pfn_SetUEF pSetUEF = (pfn_SetUEF)win32_resolve_import(
        "kernel32.dll", "SetUnhandledExceptionFilter");
    pfn_UEF pUEF = (pfn_UEF)win32_resolve_import(
        "kernel32.dll", "UnhandledExceptionFilter");
    pfn_RaiseException pRaise = (pfn_RaiseException)win32_resolve_import(
        "kernel32.dll", "RaiseException");
    pfn_RtlUnwind pUnwind = (pfn_RtlUnwind)win32_resolve_import(
        "kernel32.dll", "RtlUnwind");

    TEST_ASSERT(pSetUEF != NULL, "seh: SetUnhandledExceptionFilter resolves");
    TEST_ASSERT(pUEF != NULL, "seh: UnhandledExceptionFilter resolves");
    TEST_ASSERT(pRaise != NULL, "seh: RaiseException resolves");
    TEST_ASSERT(pUnwind != NULL, "seh: RtlUnwind resolves");

    /* Test 4: msvcrt SEH exports resolve */
    void *p_handler3 = win32_resolve_import("msvcrt.dll", "_except_handler3");
    void *p_handler4 = win32_resolve_import("msvcrt.dll", "_except_handler4");
    void *p_cxx3 = win32_resolve_import("msvcrt.dll", "__CxxFrameHandler3");
    void *p_cxxthrow = win32_resolve_import("msvcrt.dll", "_CxxThrowException");
    void *p_cookie = win32_resolve_import("msvcrt.dll", "__security_cookie");
    void *p_initcookie = win32_resolve_import("msvcrt.dll", "__security_init_cookie");
    void *p_gsfail = win32_resolve_import("msvcrt.dll", "__report_gsfailure");

    TEST_ASSERT(p_handler3 != NULL, "seh: _except_handler3 resolves");
    TEST_ASSERT(p_handler4 != NULL, "seh: _except_handler4 resolves");
    TEST_ASSERT(p_cxx3 != NULL, "seh: __CxxFrameHandler3 resolves");
    TEST_ASSERT(p_cxxthrow != NULL, "seh: _CxxThrowException resolves");
    TEST_ASSERT(p_cookie != NULL, "seh: __security_cookie resolves");
    TEST_ASSERT(p_initcookie != NULL, "seh: __security_init_cookie resolves");
    TEST_ASSERT(p_gsfail != NULL, "seh: __report_gsfailure resolves");

    /* Test 5: Security cookie value */
    DWORD *cookie = (DWORD *)p_cookie;
    TEST_ASSERT(*cookie == 0xBB40E64E, "seh: __security_cookie == 0xBB40E64E");

    /* Test 6: SetUnhandledExceptionFilter round-trip */
    LPTOP_LEVEL_EXCEPTION_FILTER prev = seh_SetUnhandledExceptionFilter(NULL);
    (void)prev;
    /* Set a dummy filter and verify we get it back */
    LPTOP_LEVEL_EXCEPTION_FILTER dummy = (LPTOP_LEVEL_EXCEPTION_FILTER)0xDEADBEEF;
    LPTOP_LEVEL_EXCEPTION_FILTER old = seh_SetUnhandledExceptionFilter(dummy);
    TEST_ASSERT(old == NULL, "seh: first SetUEF returns NULL");
    LPTOP_LEVEL_EXCEPTION_FILTER old2 = seh_SetUnhandledExceptionFilter(NULL);
    TEST_ASSERT(old2 == dummy, "seh: second SetUEF returns previous filter");

    /* Test 7: Exception code constants */
    TEST_ASSERT(EXCEPTION_ACCESS_VIOLATION == 0xC0000005, "seh: ACCESS_VIOLATION code");
    TEST_ASSERT(EXCEPTION_INT_DIVIDE_BY_ZERO == 0xC0000094, "seh: DIV_BY_ZERO code");
    TEST_ASSERT(EXCEPTION_ILLEGAL_INSTRUCTION == 0xC000001D, "seh: ILLEGAL_INSTR code");
    TEST_ASSERT(SEH_CHAIN_END == 0xFFFFFFFF, "seh: chain end sentinel");
}

/* ---- Misc Win32 Tests (Phase 13) ---- */

static void test_misc_win32(void) {
    printf("== Miscellaneous Win32 Tests ==\n");

    /* Test 1: GetSystemInfo resolves */
    void *pGetSystemInfo = win32_resolve_import("kernel32.dll", "GetSystemInfo");
    void *pGetNativeSystemInfo = win32_resolve_import("kernel32.dll", "GetNativeSystemInfo");
    TEST_ASSERT(pGetSystemInfo != NULL, "misc: GetSystemInfo resolves");
    TEST_ASSERT(pGetNativeSystemInfo != NULL, "misc: GetNativeSystemInfo resolves");

    /* Test 2: GetVersionExA resolves */
    void *pGetVersionExA = win32_resolve_import("kernel32.dll", "GetVersionExA");
    void *pGetVersion = win32_resolve_import("kernel32.dll", "GetVersion");
    TEST_ASSERT(pGetVersionExA != NULL, "misc: GetVersionExA resolves");
    TEST_ASSERT(pGetVersion != NULL, "misc: GetVersion resolves");

    /* Test 3: IsProcessorFeaturePresent resolves and works */
    typedef BOOL (WINAPI *pfn_IPFP)(DWORD);
    pfn_IPFP pIPFP = (pfn_IPFP)win32_resolve_import(
        "kernel32.dll", "IsProcessorFeaturePresent");
    TEST_ASSERT(pIPFP != NULL, "misc: IsProcessorFeaturePresent resolves");

    /* Test 4: Environment variable APIs resolve */
    void *pGetEnvA = win32_resolve_import("kernel32.dll", "GetEnvironmentVariableA");
    void *pSetEnvA = win32_resolve_import("kernel32.dll", "SetEnvironmentVariableA");
    TEST_ASSERT(pGetEnvA != NULL, "misc: GetEnvironmentVariableA resolves");
    TEST_ASSERT(pSetEnvA != NULL, "misc: SetEnvironmentVariableA resolves");

    /* Test 5: FormatMessageA resolves */
    void *pFmtMsg = win32_resolve_import("kernel32.dll", "FormatMessageA");
    TEST_ASSERT(pFmtMsg != NULL, "misc: FormatMessageA resolves");

    /* Test 6: Locale APIs resolve */
    void *pLCID = win32_resolve_import("kernel32.dll", "GetUserDefaultLCID");
    void *pLocale = win32_resolve_import("kernel32.dll", "GetLocaleInfoA");
    void *pACP = win32_resolve_import("kernel32.dll", "GetACP");
    TEST_ASSERT(pLCID != NULL, "misc: GetUserDefaultLCID resolves");
    TEST_ASSERT(pLocale != NULL, "misc: GetLocaleInfoA resolves");
    TEST_ASSERT(pACP != NULL, "misc: GetACP resolves");

    /* Test 7: Time APIs resolve */
    void *pLocalTime = win32_resolve_import("kernel32.dll", "GetLocalTime");
    void *pSysTime = win32_resolve_import("kernel32.dll", "GetSystemTime");
    void *pTZI = win32_resolve_import("kernel32.dll", "GetTimeZoneInformation");
    void *pTick = win32_resolve_import("kernel32.dll", "GetTickCount");
    void *pQPC = win32_resolve_import("kernel32.dll", "QueryPerformanceCounter");
    void *pQPF = win32_resolve_import("kernel32.dll", "QueryPerformanceFrequency");
    TEST_ASSERT(pLocalTime != NULL, "misc: GetLocalTime resolves");
    TEST_ASSERT(pSysTime != NULL, "misc: GetSystemTime resolves");
    TEST_ASSERT(pTZI != NULL, "misc: GetTimeZoneInformation resolves");
    TEST_ASSERT(pTick != NULL, "misc: GetTickCount resolves");
    TEST_ASSERT(pQPC != NULL, "misc: QueryPerformanceCounter resolves");
    TEST_ASSERT(pQPF != NULL, "misc: QueryPerformanceFrequency resolves");

    /* Test 8: Thread pool stubs resolve */
    void *pQUWI = win32_resolve_import("kernel32.dll", "QueueUserWorkItem");
    void *pCTQ = win32_resolve_import("kernel32.dll", "CreateTimerQueue");
    TEST_ASSERT(pQUWI != NULL, "misc: QueueUserWorkItem resolves");
    TEST_ASSERT(pCTQ != NULL, "misc: CreateTimerQueue resolves");

    /* Test 9: advapi32 security stubs resolve */
    void *pOPT = win32_resolve_import("advapi32.dll", "OpenProcessToken");
    void *pGTI = win32_resolve_import("advapi32.dll", "GetTokenInformation");
    void *pGUN = win32_resolve_import("advapi32.dll", "GetUserNameA");
    void *pGUNW = win32_resolve_import("advapi32.dll", "GetUserNameW");
    TEST_ASSERT(pOPT != NULL, "misc: OpenProcessToken resolves");
    TEST_ASSERT(pGTI != NULL, "misc: GetTokenInformation resolves");
    TEST_ASSERT(pGUN != NULL, "misc: GetUserNameA resolves");
    TEST_ASSERT(pGUNW != NULL, "misc: GetUserNameW resolves");

    /* Test 10: Startup info */
    void *pGSIA = win32_resolve_import("kernel32.dll", "GetStartupInfoA");
    TEST_ASSERT(pGSIA != NULL, "misc: GetStartupInfoA resolves");
}

/* ---- CRT Phase 1 Tests ---- */

static void test_crt_phase1(void) {
    printf("== CRT Phase 1 Tests ==\n");

    /* Test strtoul resolution and functionality */
    void *pStrtoul = win32_resolve_import("msvcrt.dll", "strtoul");
    TEST_ASSERT(pStrtoul != NULL, "crt: strtoul resolves");
    if (pStrtoul) {
        typedef unsigned long (*pfn_strtoul)(const char *, char **, int);
        pfn_strtoul fn = (pfn_strtoul)pStrtoul;
        TEST_ASSERT(fn("123", NULL, 10) == 123, "crt: strtoul(123) == 123");
        TEST_ASSERT(fn("FF", NULL, 16) == 255, "crt: strtoul(FF, 16) == 255");
        TEST_ASSERT(fn("0", NULL, 10) == 0, "crt: strtoul(0) == 0");
    }

    /* Test fseek/ftell resolution */
    void *pFseek = win32_resolve_import("msvcrt.dll", "fseek");
    void *pFtell = win32_resolve_import("msvcrt.dll", "ftell");
    void *pRewind = win32_resolve_import("msvcrt.dll", "rewind");
    TEST_ASSERT(pFseek != NULL, "crt: fseek resolves");
    TEST_ASSERT(pFtell != NULL, "crt: ftell resolves");
    TEST_ASSERT(pRewind != NULL, "crt: rewind resolves");

    /* Functional fseek/ftell test via libc directly */
    {
        /* Write a test file */
        const char *testdata = "ABCDEFGHIJ";
        fs_write_file("/tmp/crt_test", (const uint8_t *)testdata, 10);
        FILE *f = fopen("/tmp/crt_test", "r");
        if (f) {
            TEST_ASSERT(ftell(f) == 0, "crt: ftell initial == 0");
            fseek(f, 5, 0); /* SEEK_SET */
            TEST_ASSERT(ftell(f) == 5, "crt: ftell after seek == 5");
            int c = fgetc(f);
            TEST_ASSERT(c == 'F', "crt: fgetc after seek == 'F'");
            fseek(f, 0, 0); /* rewind */
            c = fgetc(f);
            TEST_ASSERT(c == 'A', "crt: fgetc after rewind == 'A'");
            fclose(f);
        }
    }

    /* Test time resolution and functionality */
    void *pTime = win32_resolve_import("msvcrt.dll", "time");
    void *pLocaltime = win32_resolve_import("msvcrt.dll", "localtime");
    void *pMktime = win32_resolve_import("msvcrt.dll", "mktime");
    void *pStrftime = win32_resolve_import("msvcrt.dll", "strftime");
    void *pClock = win32_resolve_import("msvcrt.dll", "clock");
    TEST_ASSERT(pTime != NULL, "crt: time resolves");
    TEST_ASSERT(pLocaltime != NULL, "crt: localtime resolves");
    TEST_ASSERT(pMktime != NULL, "crt: mktime resolves");
    TEST_ASSERT(pStrftime != NULL, "crt: strftime resolves");
    TEST_ASSERT(pClock != NULL, "crt: clock resolves");

    if (pTime) {
        typedef uint32_t (*pfn_time)(uint32_t *);
        pfn_time fn = (pfn_time)pTime;
        uint32_t t = fn(NULL);
        TEST_ASSERT(t > 0, "crt: time() returns nonzero");
    }

    /* Test signal resolution */
    void *pSignal = win32_resolve_import("msvcrt.dll", "signal");
    void *pRaise = win32_resolve_import("msvcrt.dll", "raise");
    TEST_ASSERT(pSignal != NULL, "crt: signal resolves");
    TEST_ASSERT(pRaise != NULL, "crt: raise resolves");

    /* Test locale resolution and functionality */
    void *pSetlocale = win32_resolve_import("msvcrt.dll", "setlocale");
    void *pLocaleconv = win32_resolve_import("msvcrt.dll", "localeconv");
    TEST_ASSERT(pSetlocale != NULL, "crt: setlocale resolves");
    TEST_ASSERT(pLocaleconv != NULL, "crt: localeconv resolves");

    if (pSetlocale) {
        typedef const char *(*pfn_setlocale)(int, const char *);
        pfn_setlocale fn = (pfn_setlocale)pSetlocale;
        const char *loc = fn(0, "");
        TEST_ASSERT(loc != NULL && strcmp(loc, "C") == 0, "crt: setlocale returns 'C'");
    }

    /* Test POSIX-style I/O resolution */
    void *pOpen = win32_resolve_import("msvcrt.dll", "_open");
    void *pRead = win32_resolve_import("msvcrt.dll", "_read");
    void *pWrite = win32_resolve_import("msvcrt.dll", "_write");
    void *pClose = win32_resolve_import("msvcrt.dll", "_close");
    void *pLseek = win32_resolve_import("msvcrt.dll", "_lseek");
    TEST_ASSERT(pOpen != NULL, "crt: _open resolves");
    TEST_ASSERT(pRead != NULL, "crt: _read resolves");
    TEST_ASSERT(pWrite != NULL, "crt: _write resolves");
    TEST_ASSERT(pClose != NULL, "crt: _close resolves");
    TEST_ASSERT(pLseek != NULL, "crt: _lseek resolves");

    /* Test stat/access resolution */
    void *pStat = win32_resolve_import("msvcrt.dll", "_stat");
    void *pFstat = win32_resolve_import("msvcrt.dll", "_fstat");
    void *pAccess = win32_resolve_import("msvcrt.dll", "_access");
    TEST_ASSERT(pStat != NULL, "crt: _stat resolves");
    TEST_ASSERT(pFstat != NULL, "crt: _fstat resolves");
    TEST_ASSERT(pAccess != NULL, "crt: _access resolves");

    /* Test C++ new/delete resolution */
    void *pNew = win32_resolve_import("msvcrt.dll", "??2@YAPAXI@Z");
    void *pNewArr = win32_resolve_import("msvcrt.dll", "??_U@YAPAXI@Z");
    void *pDel = win32_resolve_import("msvcrt.dll", "??3@YAXPAX@Z");
    void *pDelArr = win32_resolve_import("msvcrt.dll", "??_V@YAXPAX@Z");
    TEST_ASSERT(pNew != NULL, "crt: operator new resolves");
    TEST_ASSERT(pNewArr != NULL, "crt: operator new[] resolves");
    TEST_ASSERT(pDel != NULL, "crt: operator delete resolves");
    TEST_ASSERT(pDelArr != NULL, "crt: operator delete[] resolves");

    /* Functional: new/delete round-trip */
    if (pNew && pDel) {
        typedef void *(*pfn_new)(size_t);
        typedef void (*pfn_del)(void *);
        pfn_new fn_new = (pfn_new)pNew;
        pfn_del fn_del = (pfn_del)pDel;
        void *p = fn_new(64);
        TEST_ASSERT(p != NULL, "crt: operator new(64) != NULL");
        if (p) { memset(p, 0xAA, 64); fn_del(p); }
        TEST_ASSERT(1, "crt: operator delete no crash");
    }

    /* Test ctype completions */
    void *pIsUpper = win32_resolve_import("msvcrt.dll", "isupper");
    void *pIsLower = win32_resolve_import("msvcrt.dll", "islower");
    void *pIsPrint = win32_resolve_import("msvcrt.dll", "isprint");
    void *pIsXdigit = win32_resolve_import("msvcrt.dll", "isxdigit");
    TEST_ASSERT(pIsUpper != NULL, "crt: isupper resolves");
    TEST_ASSERT(pIsLower != NULL, "crt: islower resolves");
    TEST_ASSERT(pIsPrint != NULL, "crt: isprint resolves");
    TEST_ASSERT(pIsXdigit != NULL, "crt: isxdigit resolves");

    /* Test math stubs resolution */
    void *pSqrt = win32_resolve_import("msvcrt.dll", "sqrt");
    void *pFabs = win32_resolve_import("msvcrt.dll", "fabs");
    void *pSin = win32_resolve_import("msvcrt.dll", "sin");
    void *pPow = win32_resolve_import("msvcrt.dll", "pow");
    TEST_ASSERT(pSqrt != NULL, "crt: sqrt resolves");
    TEST_ASSERT(pFabs != NULL, "crt: fabs resolves");
    TEST_ASSERT(pSin != NULL, "crt: sin resolves");
    TEST_ASSERT(pPow != NULL, "crt: pow resolves");

    /* Test string additions */
    void *pStricmp = win32_resolve_import("msvcrt.dll", "_stricmp");
    void *pStrdup2 = win32_resolve_import("msvcrt.dll", "_strdup");
    void *pStrerror = win32_resolve_import("msvcrt.dll", "strerror");
    TEST_ASSERT(pStricmp != NULL, "crt: _stricmp resolves");
    TEST_ASSERT(pStrdup2 != NULL, "crt: _strdup resolves");
    TEST_ASSERT(pStrerror != NULL, "crt: strerror resolves");

    /* Test global state */
    void *pAcmdln = win32_resolve_import("msvcrt.dll", "_acmdln");
    void *pArgc = win32_resolve_import("msvcrt.dll", "__argc");
    void *pEnviron = win32_resolve_import("msvcrt.dll", "_environ");
    TEST_ASSERT(pAcmdln != NULL, "crt: _acmdln resolves");
    TEST_ASSERT(pArgc != NULL, "crt: __argc resolves");
    TEST_ASSERT(pEnviron != NULL, "crt: _environ resolves");

    /* Test RTTI stubs */
    void *pRTtypeid = win32_resolve_import("msvcrt.dll", "__RTtypeid");
    void *pRTDynCast = win32_resolve_import("msvcrt.dll", "__RTDynamicCast");
    void *pTypeInfoVtable = win32_resolve_import("msvcrt.dll", "??_7type_info@@6B@");
    TEST_ASSERT(pRTtypeid != NULL, "crt: __RTtypeid resolves");
    TEST_ASSERT(pRTDynCast != NULL, "crt: __RTDynamicCast resolves");
    TEST_ASSERT(pTypeInfoVtable != NULL, "crt: type_info vtable resolves");
}

/* ---- SEH Phase 2 Tests ---- */

static void test_seh_phase2(void) {
    printf("== SEH Phase 2 Tests ==\n");

    /* Test 1: VEH exports resolve from kernel32 */
    void *pAddVEH = win32_resolve_import("kernel32.dll", "AddVectoredExceptionHandler");
    void *pRemoveVEH = win32_resolve_import("kernel32.dll", "RemoveVectoredExceptionHandler");
    void *pAddVCH = win32_resolve_import("kernel32.dll", "AddVectoredContinueHandler");
    void *pRemoveVCH = win32_resolve_import("kernel32.dll", "RemoveVectoredContinueHandler");
    TEST_ASSERT(pAddVEH != NULL, "seh2: AddVectoredExceptionHandler resolves");
    TEST_ASSERT(pRemoveVEH != NULL, "seh2: RemoveVectoredExceptionHandler resolves");
    TEST_ASSERT(pAddVCH != NULL, "seh2: AddVectoredContinueHandler resolves");
    TEST_ASSERT(pRemoveVCH != NULL, "seh2: RemoveVectoredContinueHandler resolves");

    /* Test 2: VEH add/remove round-trip */
    {
        /* Dummy handler — never actually called */
        LONG dummy_handler(EXCEPTION_POINTERS *ep) {
            (void)ep;
            return EXCEPTION_CONTINUE_SEARCH;
        }
        PVOID h = seh_AddVectoredExceptionHandler(0, (PVECTORED_EXCEPTION_HANDLER)dummy_handler);
        TEST_ASSERT(h != NULL, "seh2: AddVectoredExceptionHandler returns handle");
        ULONG removed = seh_RemoveVectoredExceptionHandler(h);
        TEST_ASSERT(removed == 1, "seh2: RemoveVectoredExceptionHandler succeeds");
        /* Remove again should fail */
        removed = seh_RemoveVectoredExceptionHandler(h);
        TEST_ASSERT(removed == 0, "seh2: double-remove returns 0");
    }

    /* Test 3: VEH continue handler add/remove */
    {
        LONG dummy_cont(EXCEPTION_POINTERS *ep) {
            (void)ep;
            return EXCEPTION_CONTINUE_SEARCH;
        }
        PVOID h = seh_AddVectoredContinueHandler(1, (PVECTORED_EXCEPTION_HANDLER)dummy_cont);
        TEST_ASSERT(h != NULL, "seh2: AddVectoredContinueHandler returns handle");
        ULONG removed = seh_RemoveVectoredContinueHandler(h);
        TEST_ASSERT(removed == 1, "seh2: RemoveVectoredContinueHandler succeeds");
    }

    /* Test 4: C++ exception infrastructure exports resolve */
    void *pCppFilter = win32_resolve_import("msvcrt.dll", "__CppXcptFilter");
    void *pSetSeTrans = win32_resolve_import("msvcrt.dll", "_set_se_translator");
    TEST_ASSERT(pCppFilter != NULL, "seh2: __CppXcptFilter resolves");
    TEST_ASSERT(pSetSeTrans != NULL, "seh2: _set_se_translator resolves");

    /* Test 5: _set_se_translator round-trip */
    {
        _se_translator_function prev = seh_set_se_translator(NULL);
        TEST_ASSERT(prev == NULL, "seh2: initial se_translator is NULL");
        /* Set a dummy translator */
        void dummy_trans(unsigned int code, EXCEPTION_POINTERS *ep) {
            (void)code; (void)ep;
        }
        _se_translator_function old = seh_set_se_translator(dummy_trans);
        TEST_ASSERT(old == NULL, "seh2: first set_se_translator returns NULL");
        old = seh_set_se_translator(NULL);
        TEST_ASSERT(old == (_se_translator_function)dummy_trans, "seh2: second set_se_translator returns previous");
    }

    /* Test 6: setjmp/longjmp resolve from msvcrt */
    void *pSetjmp = win32_resolve_import("msvcrt.dll", "setjmp");
    void *pLongjmp = win32_resolve_import("msvcrt.dll", "longjmp");
    void *p_Setjmp = win32_resolve_import("msvcrt.dll", "_setjmp");
    void *p_Longjmp = win32_resolve_import("msvcrt.dll", "_longjmp");
    TEST_ASSERT(pSetjmp != NULL, "seh2: setjmp resolves");
    TEST_ASSERT(pLongjmp != NULL, "seh2: longjmp resolves");
    TEST_ASSERT(p_Setjmp != NULL, "seh2: _setjmp resolves");
    TEST_ASSERT(p_Longjmp != NULL, "seh2: _longjmp resolves");

    /* Test 7: setjmp/longjmp functional test */
    {
        jmp_buf env;
        volatile int reached = 0;
        int val = setjmp(env);
        if (val == 0) {
            /* First return from setjmp */
            reached = 1;
            longjmp(env, 42);
            /* Should not reach here */
            reached = 99;
        } else {
            /* Returned from longjmp */
            TEST_ASSERT(val == 42, "seh2: longjmp returns correct value");
            TEST_ASSERT(reached == 1, "seh2: setjmp initial path was taken");
        }
        TEST_ASSERT(reached != 99, "seh2: code after longjmp not reached");
    }

    /* Test 8: Guard page constants */
    TEST_ASSERT(STATUS_GUARD_PAGE_VIOLATION == 0x80000001, "seh2: STATUS_GUARD_PAGE_VIOLATION code");
    TEST_ASSERT(PAGE_GUARD == 0x100, "seh2: PAGE_GUARD value");

    /* Test 9: Guard page PTE flag defined */
    TEST_ASSERT(PTE_GUARD == 0x200, "seh2: PTE_GUARD flag == 0x200");

    /* Test 10: VEH dispatch with empty handler list returns CONTINUE_SEARCH */
    {
        EXCEPTION_RECORD er;
        memset(&er, 0, sizeof(er));
        er.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
        CONTEXT ctx;
        memset(&ctx, 0, sizeof(ctx));
        EXCEPTION_POINTERS ep;
        ep.ExceptionRecord = &er;
        ep.ContextRecord = &ctx;
        LONG result = seh_dispatch_vectored(&ep);
        TEST_ASSERT(result == EXCEPTION_CONTINUE_SEARCH,
                    "seh2: empty VEH dispatch returns CONTINUE_SEARCH");
    }

    /* Test 11: Exception disposition constants */
    TEST_ASSERT(ExceptionContinueExecution == 0, "seh2: ExceptionContinueExecution == 0");
    TEST_ASSERT(ExceptionContinueSearch == 1, "seh2: ExceptionContinueSearch == 1");
    TEST_ASSERT(EXCEPTION_CONTINUE_EXECUTION == -1, "seh2: EXCEPTION_CONTINUE_EXECUTION == -1");
    TEST_ASSERT(EXCEPTION_CONTINUE_SEARCH == 0, "seh2: EXCEPTION_CONTINUE_SEARCH == 0");
}

/* ---- Phase 2: Process Model & Scheduling Tests ---- */

static void test_phase7_win32(void) {
    printf("== Phase 7 Win32 Hardening Tests ==\n");

    /* ── PE loader: read_file_to_buffer no longer allocates 4GB ──── */
    /* Create a small test file and verify pe_load works with sized reads */
    {
        const char *testfile = "/pe7_test.dat";
        char data[] = "MZ_test_data_12345";
        fs_create_file(testfile, 0);
        fs_write_file(testfile, (uint8_t *)data, sizeof(data));

        /* Verify the file is readable via fs_resolve_path + fs_read_inode */
        uint32_t parent;
        char fname[28];
        int ino = fs_resolve_path(testfile, &parent, fname);
        TEST_ASSERT(ino >= 0, "pe7: test file created and resolvable");
        if (ino >= 0) {
            inode_t node;
            fs_read_inode(ino, &node);
            TEST_ASSERT(node.size == sizeof(data), "pe7: file size matches written data");
            TEST_ASSERT(node.size < 1024, "pe7: file size is small (not 4GB)");
        }
        fs_delete_file(testfile);
    }

    /* ── PE concurrency: pe_ctxs per-task context ────────────────── */
    /* pe_get_command_line should return empty for non-PE tasks */
    {
        const char *cmd = pe_get_command_line(0);
        TEST_ASSERT(cmd != NULL, "pe7: pe_get_command_line(0) non-null");
    }

    /* ── PE address reclamation ──────────────────────────────────── */
    /* pe_loaded_image_t size is reasonable */
    TEST_ASSERT(sizeof(pe_loaded_image_t) <= 128, "pe7: pe_loaded_image_t fits stack");

    /* ── SEH constants (verify correct values) ──────────────────── */
    TEST_ASSERT(EXCEPTION_ACCESS_VIOLATION == 0xC0000005, "pe7: STATUS_ACCESS_VIOLATION");
    TEST_ASSERT(EXCEPTION_INT_DIVIDE_BY_ZERO == 0xC0000094, "pe7: STATUS_INT_DIVIDE_BY_ZERO");
    TEST_ASSERT(EXCEPTION_BREAKPOINT == 0x80000003, "pe7: STATUS_BREAKPOINT");
    TEST_ASSERT(EXCEPTION_ILLEGAL_INSTRUCTION == 0xC000001D, "pe7: STATUS_ILLEGAL_INSTRUCTION");
    TEST_ASSERT(EXCEPTION_STACK_OVERFLOW == 0xC00000FD, "pe7: STATUS_STACK_OVERFLOW");
    TEST_ASSERT(SEH_CHAIN_END == 0xFFFFFFFF, "pe7: SEH_CHAIN_END");

    /* ── SEH struct sizes ───────────────────────────────────────── */
    TEST_ASSERT(sizeof(EXCEPTION_RECORD) >= 20, "pe7: EXCEPTION_RECORD >= 20 bytes");
    TEST_ASSERT(sizeof(CONTEXT) >= 40, "pe7: CONTEXT >= 40 bytes");

    /* ── Console: GetStdHandle returns valid handles ─────────────── */
    {
        typedef uint32_t (__attribute__((stdcall)) *pfn_GetStdHandle)(uint32_t);
        pfn_GetStdHandle pGSH = (pfn_GetStdHandle)win32_resolve_import(
            "kernel32.dll", "GetStdHandle");
        TEST_ASSERT(pGSH != NULL, "pe7: GetStdHandle resolved");
        if (pGSH) {
            uint32_t hIn  = pGSH(0xFFFFFFF6); /* STD_INPUT_HANDLE = -10 */
            uint32_t hOut = pGSH(0xFFFFFFF5); /* STD_OUTPUT_HANDLE = -11 */
            uint32_t hErr = pGSH(0xFFFFFFF4); /* STD_ERROR_HANDLE = -12 */
            TEST_ASSERT(hIn != 0xFFFFFFFF, "pe7: GetStdHandle(stdin) valid");
            TEST_ASSERT(hOut != 0xFFFFFFFF, "pe7: GetStdHandle(stdout) valid");
            TEST_ASSERT(hErr != 0xFFFFFFFF, "pe7: GetStdHandle(stderr) valid");
        }
    }

    /* ── Console: WriteConsoleA/ReadConsoleA resolved ────────────── */
    TEST_ASSERT(win32_resolve_import("kernel32.dll", "WriteConsoleA") != NULL,
                "pe7: WriteConsoleA resolved");
    TEST_ASSERT(win32_resolve_import("kernel32.dll", "ReadConsoleA") != NULL,
                "pe7: ReadConsoleA resolved");
    TEST_ASSERT(win32_resolve_import("kernel32.dll", "GetConsoleMode") != NULL,
                "pe7: GetConsoleMode resolved");
    TEST_ASSERT(win32_resolve_import("kernel32.dll", "SetConsoleMode") != NULL,
                "pe7: SetConsoleMode resolved");
    TEST_ASSERT(win32_resolve_import("kernel32.dll", "SetConsoleTitleA") != NULL,
                "pe7: SetConsoleTitleA resolved");
    TEST_ASSERT(win32_resolve_import("kernel32.dll", "GetConsoleScreenBufferInfo") != NULL,
                "pe7: GetConsoleScreenBufferInfo resolved");
    TEST_ASSERT(win32_resolve_import("kernel32.dll", "SetConsoleTextAttribute") != NULL,
                "pe7: SetConsoleTextAttribute resolved");

    /* ── WaitFor: WaitForSingleObject with 0ms timeout ──────────── */
    {
        typedef uint32_t (__attribute__((stdcall)) *pfn_CreateEventA)(
            void *, int, int, const char *);
        typedef uint32_t (__attribute__((stdcall)) *pfn_WaitForSingleObject)(
            uint32_t, uint32_t);
        typedef int (__attribute__((stdcall)) *pfn_SetEvent)(uint32_t);
        typedef int (__attribute__((stdcall)) *pfn_CloseHandle)(uint32_t);

        pfn_CreateEventA pCE = (pfn_CreateEventA)win32_resolve_import(
            "kernel32.dll", "CreateEventA");
        pfn_WaitForSingleObject pWFSO = (pfn_WaitForSingleObject)
            win32_resolve_import("kernel32.dll", "WaitForSingleObject");
        pfn_SetEvent pSE = (pfn_SetEvent)win32_resolve_import(
            "kernel32.dll", "SetEvent");
        pfn_CloseHandle pCH = (pfn_CloseHandle)win32_resolve_import(
            "kernel32.dll", "CloseHandle");

        TEST_ASSERT(pCE != NULL, "pe7: CreateEventA resolved");
        TEST_ASSERT(pWFSO != NULL, "pe7: WaitForSingleObject resolved");
        TEST_ASSERT(pSE != NULL, "pe7: SetEvent resolved");

        if (pCE && pWFSO && pSE && pCH) {
            /* Create unsignaled auto-reset event */
            uint32_t hEvent = pCE(NULL, 0, 0, NULL);
            TEST_ASSERT(hEvent != 0xFFFFFFFF, "pe7: CreateEventA success");

            /* Wait with 0 timeout on unsignaled event → WAIT_TIMEOUT */
            uint32_t r = pWFSO(hEvent, 0);
            TEST_ASSERT(r == 258 /* WAIT_TIMEOUT */, "pe7: WaitFor unsignaled returns TIMEOUT");

            /* Signal event, then wait → WAIT_OBJECT_0 */
            pSE(hEvent);
            r = pWFSO(hEvent, 0);
            TEST_ASSERT(r == 0 /* WAIT_OBJECT_0 */, "pe7: WaitFor signaled returns OBJECT_0");

            /* Auto-reset: second wait should timeout */
            r = pWFSO(hEvent, 0);
            TEST_ASSERT(r == 258 /* WAIT_TIMEOUT */, "pe7: auto-reset event re-unsignaled");

            pCH(hEvent);
        }
    }

    /* ── GetCommandLineA returns non-null ────────────────────────── */
    {
        typedef const char * (__attribute__((stdcall)) *pfn_GetCommandLineA)(void);
        pfn_GetCommandLineA pGCLA = (pfn_GetCommandLineA)win32_resolve_import(
            "kernel32.dll", "GetCommandLineA");
        TEST_ASSERT(pGCLA != NULL, "pe7: GetCommandLineA resolved");
        if (pGCLA) {
            const char *cmd = pGCLA();
            TEST_ASSERT(cmd != NULL, "pe7: GetCommandLineA returns non-null");
            TEST_ASSERT(strlen(cmd) > 0, "pe7: GetCommandLineA returns non-empty");
        }
    }

    /* ── GetEnvironmentStringsA returns non-null block ───────────── */
    {
        typedef char * (__attribute__((stdcall)) *pfn_GetEnvStringsA)(void);
        typedef int (__attribute__((stdcall)) *pfn_FreeEnvStringsA)(char *);
        pfn_GetEnvStringsA pGES = (pfn_GetEnvStringsA)win32_resolve_import(
            "kernel32.dll", "GetEnvironmentStringsA");
        pfn_FreeEnvStringsA pFES = (pfn_FreeEnvStringsA)win32_resolve_import(
            "kernel32.dll", "FreeEnvironmentStringsA");
        TEST_ASSERT(pGES != NULL, "pe7: GetEnvironmentStringsA resolved");
        TEST_ASSERT(pFES != NULL, "pe7: FreeEnvironmentStringsA resolved");

        if (pGES && pFES) {
            char *block = pGES();
            TEST_ASSERT(block != NULL, "pe7: GetEnvironmentStringsA non-null");
            if (block) {
                /* First entry should be non-empty (at least USER=root) */
                TEST_ASSERT(strlen(block) > 0, "pe7: env block has entries");
                pFES(block);
            }
        }
    }

    /* ── ExpandEnvironmentStringsA ───────────────────────────────── */
    {
        typedef uint32_t (__attribute__((stdcall)) *pfn_ExpandEnvA)(
            const char *, char *, uint32_t);
        pfn_ExpandEnvA pEEA = (pfn_ExpandEnvA)win32_resolve_import(
            "kernel32.dll", "ExpandEnvironmentStringsA");
        TEST_ASSERT(pEEA != NULL, "pe7: ExpandEnvironmentStringsA resolved");
        if (pEEA) {
            /* Set a known env var and expand it */
            env_set("TESTVAR", "hello");
            char buf[128];
            uint32_t needed = pEEA("%TESTVAR%_world", buf, sizeof(buf));
            TEST_ASSERT(needed > 0, "pe7: ExpandEnv returns needed size");
            TEST_ASSERT(strcmp(buf, "hello_world") == 0, "pe7: ExpandEnv expands %TESTVAR%");
            env_unset("TESTVAR");
        }
    }

    /* ── VEH handlers (add/remove round-trip) ───────────────────── */
    TEST_ASSERT(win32_resolve_import("kernel32.dll", "AddVectoredExceptionHandler") != NULL,
                "pe7: AddVectoredExceptionHandler resolved");
    TEST_ASSERT(win32_resolve_import("kernel32.dll", "RemoveVectoredExceptionHandler") != NULL,
                "pe7: RemoveVectoredExceptionHandler resolved");
    TEST_ASSERT(win32_resolve_import("kernel32.dll", "RaiseException") != NULL,
                "pe7: RaiseException resolved");
    TEST_ASSERT(win32_resolve_import("kernel32.dll", "RtlUnwind") != NULL,
                "pe7: RtlUnwind resolved");

    /* ── Futex integration: verify sys_futex callable ───────────── */
    {
        volatile uint32_t futex_var = 42;
        /* FUTEX_WAKE on an address with no waiters should return 0 */
        int woken = sys_futex((uint32_t *)&futex_var, 1 /* FUTEX_WAKE */, 1);
        TEST_ASSERT(woken == 0, "pe7: futex_wake with no waiters returns 0");
    }

    /* ── env_get_entry enumeration ──────────────────────────────── */
    {
        int found = 0;
        for (int i = 0; i < MAX_ENV_VARS; i++) {
            const char *name, *value;
            if (env_get_entry(i, &name, &value)) {
                found++;
                TEST_ASSERT(name != NULL && name[0] != '\0', "pe7: env entry has name");
            }
        }
        TEST_ASSERT(found > 0, "pe7: env_get_entry found active entries");
    }
}

void test_win32_all(void) {
    test_win32_dll();
    test_win32_registry();
    test_winsock();
    test_gdi_advanced();
    test_com_ole();
    test_unicode_wide();
    test_unicode_i18n();
    test_seh();
    test_misc_win32();
    test_crt_phase1();
    test_seh_phase2();
    test_phase7_win32();
}
