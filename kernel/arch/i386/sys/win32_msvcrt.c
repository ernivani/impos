#include <kernel/win32_types.h>
#include <kernel/win32_seh.h>
#include <kernel/task.h>
#include <kernel/io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── msvcrt shim ─────────────────────────────────────────────
 * Most of msvcrt maps directly to our libc functions.
 * We just need to export them with the right names.
 * ─────────────────────────────────────────────────────────── */

/* ── I/O (simplified — console only for now) ─────────────────── */

static int shim_puts(const char *s) {
    return puts(s);
}

static int shim_putchar(int c) {
    return putchar(c);
}

static int shim_printf(const char *fmt, ...) {
    /* We can't easily do varargs forwarding in our shim,
     * but since PE code will call through the IAT with its own
     * va_list, the actual printf will get called directly.
     * This shim is here for name resolution. */
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int ret = vsnprintf(NULL, 0, fmt, ap);  /* count only */
    __builtin_va_end(ap);

    /* Actually print */
    char buf[512];
    __builtin_va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    __builtin_va_end(ap);

    for (int i = 0; buf[i]; i++)
        putchar(buf[i]);
    return ret;
}

static int shim_sprintf(char *buf, const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int ret = vsnprintf(buf, 4096, fmt, ap);
    __builtin_va_end(ap);
    return ret;
}

static int shim_snprintf(char *buf, size_t n, const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int ret = vsnprintf(buf, n, fmt, ap);
    __builtin_va_end(ap);
    return ret;
}

static int shim_fprintf(void *stream, const char *fmt, ...) {
    (void)stream;
    /* Print to console regardless of stream */
    char buf[512];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    __builtin_va_end(ap);
    for (int i = 0; buf[i]; i++)
        putchar(buf[i]);
    return (int)strlen(buf);
}

static int shim_sscanf(const char *str, const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int ret = sscanf(str, fmt, ap);
    __builtin_va_end(ap);
    return ret;
}

/* ── String Functions ────────────────────────────────────────── */
/* These map directly since our libc has them all. */

/* ── Memory ──────────────────────────────────────────────────── */

static void shim_exit(int status) {
    printf("[msvcrt] exit(%d)\n", status);
    extern void task_exit(void);
    task_exit();
}

static void shim_abort(void) {
    printf("[msvcrt] abort()\n");
    extern void task_exit(void);
    task_exit();
}

/* ── Math ────────────────────────────────────────────────────── */
/* Basic integer math only — no FPU in our kernel */

static int shim_abs(int x) { return x < 0 ? -x : x; }

/* ── ctype ───────────────────────────────────────────────────── */

static int shim_isdigit(int c) { return c >= '0' && c <= '9'; }
static int shim_isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int shim_isalnum(int c) { return shim_isdigit(c) || shim_isalpha(c); }
static int shim_isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
static int shim_toupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
static int shim_tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

/* ── MSVC-specific functions ─────────────────────────────────── */

/* _initterm: call function pointers between start and end */
static void shim__initterm(void (**start)(void), void (**end)(void)) {
    while (start < end) {
        if (*start)
            (*start)();
        start++;
    }
}

static int shim__initterm_e(int (**start)(void), int (**end)(void)) {
    while (start < end) {
        if (*start) {
            int ret = (*start)();
            if (ret != 0) return ret;
        }
        start++;
    }
    return 0;
}

/* CRT init stubs */
static int shim___p___argc(void) { return 0; }
static void *shim___p___argv(void) { return NULL; }
static void shim__set_app_type(int type) { (void)type; }
static int shim___set_app_type(int type) { (void)type; return 0; }
static void *shim__get_initial_narrow_environment(void) { return NULL; }
static int shim__configure_narrow_argv(int mode) { (void)mode; return 0; }
static int shim__initialize_narrow_environment(void) { return 0; }
static int shim__crt_atexit(void (*func)(void)) { (void)func; return 0; }
static void shim__cexit(void) {}
static void shim__c_exit(void) {}
static int shim__controlfp_s(unsigned int *cur, unsigned int newval, unsigned int mask) {
    (void)cur; (void)newval; (void)mask;
    return 0;
}
static void *shim___acrt_iob_func(unsigned idx) {
    (void)idx;
    /* Return a fake FILE* — we only support console output */
    static int fake_file;
    return &fake_file;
}
static int shim__register_onexit_function(void *table, void *func) {
    (void)table; (void)func;
    return 0;
}
static void *shim__crt_at_quick_exit(void *func) { (void)func; return NULL; }

/* errno */
static int shim_errno_val = 0;
static int *shim__errno(void) { return &shim_errno_val; }

/* ── Threading (msvcrt wrappers around kernel32) ───────────── */

/* Forward-declare the kernel32 shim functions we need */
extern HANDLE WINAPI shim_CreateThread(
    LPVOID lpThreadAttributes, DWORD dwStackSize,
    LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter,
    DWORD dwCreationFlags, LPDWORD lpThreadId);
extern void WINAPI shim_ExitThread(DWORD dwExitCode);

/* _beginthreadex returns a handle (cast to uintptr_t), 0 on failure */
static unsigned int shim__beginthreadex(
    void *security, unsigned stack_size,
    unsigned int (__attribute__((stdcall)) *start_address)(void *),
    void *arglist, unsigned initflag, unsigned *thrdaddr)
{
    DWORD tid = 0;
    HANDLE h = shim_CreateThread(
        security, stack_size,
        (LPTHREAD_START_ROUTINE)start_address,
        arglist, initflag, &tid);
    if (thrdaddr) *thrdaddr = tid;
    return (unsigned int)h;
}

static void shim__endthreadex(unsigned retval) {
    shim_ExitThread((DWORD)retval);
}

/* ── Delay-Load Helper ───────────────────────────────────────── */

/* Minimal delay-load descriptor — matches MSVC's ImgDelayDescr */
typedef struct {
    uint32_t grAttrs;
    uint32_t rvaDLLName;
    uint32_t rvaHmod;
    uint32_t rvaIAT;
    uint32_t rvaINT;
    uint32_t rvaBoundIAT;
    uint32_t rvaUnloadIAT;
    uint32_t dwTimeStamp;
} ImgDelayDescr;

/* Resolve LoadLibraryA/GetProcAddress at runtime via our own shim tables */
typedef HMODULE (WINAPI *pfn_LoadLibraryA)(LPCSTR);
typedef void * (WINAPI *pfn_GetProcAddress)(HMODULE, LPCSTR);

/* __delayLoadHelper2 — called by compiler-generated delay-load thunks.
 * Loads the DLL and resolves the requested function on first call. */
static void *shim___delayLoadHelper2(const ImgDelayDescr *pidd, void **ppfnIATEntry) {
    (void)ppfnIATEntry;

    if (!pidd) return NULL;

    /* Resolve LoadLibraryA from our own shim tables */
    pfn_LoadLibraryA pLoadLibraryA =
        (pfn_LoadLibraryA)win32_resolve_import("kernel32.dll", "LoadLibraryA");
    if (!pLoadLibraryA) return NULL;

    /* The DLL name RVA is relative to the module base.
     * For simplicity, treat it as an absolute pointer (works in identity-mapped space). */
    const char *dll_name = (const char *)(pidd->rvaDLLName);
    if (!dll_name || !dll_name[0]) return NULL;

    HMODULE hmod = pLoadLibraryA(dll_name);
    if (!hmod) {
        printf("[delayload] failed to load '%s'\n", dll_name);
        return NULL;
    }

    /* Store module handle if slot is provided */
    if (pidd->rvaHmod) {
        HMODULE *phmod = (HMODULE *)(pidd->rvaHmod);
        *phmod = hmod;
    }

    return (void *)hmod;
}

/* ── Wide String Functions ────────────────────────────────────── */

static size_t shim_wcslen(const WCHAR *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

static WCHAR *shim_wcscpy(WCHAR *dst, const WCHAR *src) {
    WCHAR *d = dst;
    while ((*d++ = *src++));
    return dst;
}

static WCHAR *shim_wcsncpy(WCHAR *dst, const WCHAR *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i]; i++)
        dst[i] = src[i];
    for (; i < n; i++)
        dst[i] = 0;
    return dst;
}

static WCHAR *shim_wcscat(WCHAR *dst, const WCHAR *src) {
    WCHAR *d = dst;
    while (*d) d++;
    while ((*d++ = *src++));
    return dst;
}

static int shim_wcscmp(const WCHAR *a, const WCHAR *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)*a - (int)*b;
}

static int shim_wcsncmp(const WCHAR *a, const WCHAR *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)a[i] - (int)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}

static WCHAR *shim_wcschr(const WCHAR *s, WCHAR c) {
    while (*s) {
        if (*s == c) return (WCHAR *)s;
        s++;
    }
    return c == 0 ? (WCHAR *)s : NULL;
}

static WCHAR *shim_wcsrchr(const WCHAR *s, WCHAR c) {
    const WCHAR *last = NULL;
    while (*s) {
        if (*s == c) last = s;
        s++;
    }
    if (c == 0) return (WCHAR *)s;
    return (WCHAR *)last;
}

static WCHAR *shim_wcsstr(const WCHAR *haystack, const WCHAR *needle) {
    if (!needle[0]) return (WCHAR *)haystack;
    size_t nlen = shim_wcslen(needle);
    for (; *haystack; haystack++) {
        if (shim_wcsncmp(haystack, needle, nlen) == 0)
            return (WCHAR *)haystack;
    }
    return NULL;
}

static WCHAR *shim__wcsdup(const WCHAR *s) {
    size_t len = shim_wcslen(s) + 1;
    WCHAR *dup = (WCHAR *)malloc(len * sizeof(WCHAR));
    if (dup) memcpy(dup, s, len * sizeof(WCHAR));
    return dup;
}

static int shim_wprintf(const WCHAR *fmt, ...) {
    /* Convert format to narrow and print */
    char narrow[512];
    extern int win32_wchar_to_utf8(const WCHAR *, int, char *, int);
    win32_wchar_to_utf8(fmt, -1, narrow, sizeof(narrow));
    printf("%s", narrow);
    return (int)strlen(narrow);
}

static int shim_swprintf(WCHAR *buf, size_t n, const WCHAR *fmt, ...) {
    (void)buf; (void)n; (void)fmt;
    if (buf && n > 0) buf[0] = 0;
    return 0;
}

static int shim__wtoi(const WCHAR *s) {
    int val = 0, neg = 0;
    if (!s) return 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    return neg ? -val : val;
}

static void *shim__wfopen(const WCHAR *filename, const WCHAR *mode) {
    char fn[256], md[16];
    extern int win32_wchar_to_utf8(const WCHAR *, int, char *, int);
    win32_wchar_to_utf8(filename, -1, fn, sizeof(fn));
    win32_wchar_to_utf8(mode, -1, md, sizeof(md));
    return fopen(fn, md);
}

static WCHAR shim_towupper(WCHAR c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    return c;
}

static WCHAR shim_towlower(WCHAR c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

/* ── Security Cookie ─────────────────────────────────────────── */

/* Global security cookie — MSVC's /GS buffer overrun protection.
 * Must be a well-known value that the compiler checks at function epilogue. */
DWORD __security_cookie = 0xBB40E64E;

static void shim___security_init_cookie(void) {
    /* In a real OS, randomize the cookie. For now, keep the default. */
}

static void shim___report_gsfailure(void) {
    serial_printf("[msvcrt] __report_gsfailure: buffer overrun detected!\n");
    printf("[msvcrt] buffer overrun detected — killing task\n");
    extern void task_exit(void);
    task_exit();
}

/* shim___security_cookie_ptr removed — __security_cookie exported directly */

/* ── SEH Frame Handlers ─────────────────────────────────────── */

/* MSVC scope table entry for _except_handler3 */
typedef struct {
    int32_t  enclosing_level;   /* -1 = outermost */
    void    *filter;            /* __except filter function, or NULL for __finally */
    void    *handler;           /* __except/__finally body */
} seh_scope_table_entry_t;

/* _except_handler3 — SEH frame walker for MSVC-compiled code.
 * Called by the OS for each frame during exception dispatch.
 * Walks the scope table to find matching __except filters. */
static EXCEPTION_DISPOSITION shim__except_handler3(
    EXCEPTION_RECORD *er, void *frame,
    CONTEXT *ctx, void *dispatcher_ctx)
{
    (void)ctx;
    (void)dispatcher_ctx;

    /* If unwinding, just return */
    if (er->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND))
        return ExceptionContinueSearch;

    /* The frame layout for MSVC is:
     * frame[-1] = trylevel (current scope depth)
     * frame[+0] = Next pointer
     * frame[+4] = Handler (_except_handler3)
     * frame[+8] = scope table pointer
     * frame[+12] = trylevel
     * We approximate by reading fields relative to the frame pointer. */

    /* Get scope table and trylevel from the registration frame.
     * In MSVC's calling convention for _except_handler3:
     *   [frame + 8] = scope_table pointer
     *   [frame + 12] = trylevel */
    uint32_t *fp = (uint32_t *)frame;
    seh_scope_table_entry_t *scope_table = (seh_scope_table_entry_t *)fp[2];
    int32_t trylevel = (int32_t)fp[3];

    if (!scope_table)
        return ExceptionContinueSearch;

    /* Walk scope table from current trylevel up to outermost */
    while (trylevel >= 0) {
        seh_scope_table_entry_t *entry = &scope_table[trylevel];

        if (entry->filter) {
            /* Call the filter.
             * Filter signature: int filter(EXCEPTION_POINTERS *ep)
             * Returns: EXCEPTION_EXECUTE_HANDLER, CONTINUE_SEARCH, or CONTINUE_EXECUTION */
            typedef int (*filter_fn_t)(void);
            /* In real MSVC code, the filter has access to the exception pointers
             * through __exception_info() / GetExceptionInformation().
             * For now, just call the filter. */
            filter_fn_t filter = (filter_fn_t)entry->filter;
            int result = filter();

            if (result == EXCEPTION_EXECUTE_HANDLER) {
                /* Found a handler — execute it */
                serial_printf("[SEH] _except_handler3: executing handler at level %d\n",
                              trylevel);
                return ExceptionContinueSearch; /* Let the OS call us back during unwind */
            } else if (result == EXCEPTION_CONTINUE_EXECUTION) {
                return ExceptionContinueExecution;
            }
            /* EXCEPTION_CONTINUE_SEARCH: try enclosing level */
        }

        trylevel = entry->enclosing_level;
    }

    return ExceptionContinueSearch;
}

/* _except_handler4 — enhanced version (with security cookie XOR).
 * For now, forwards to handler3. */
static EXCEPTION_DISPOSITION shim__except_handler4(
    EXCEPTION_RECORD *er, void *frame,
    CONTEXT *ctx, void *dispatcher_ctx)
{
    return shim__except_handler3(er, frame, ctx, dispatcher_ctx);
}

/* __CxxFrameHandler3 — C++ exception handler (try/catch via SEH).
 * Full implementation requires catchable type matching; stub for now. */
static EXCEPTION_DISPOSITION shim___CxxFrameHandler3(
    EXCEPTION_RECORD *er, void *frame,
    CONTEXT *ctx, void *dispatcher_ctx)
{
    (void)frame; (void)ctx; (void)dispatcher_ctx;

    if (er->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND))
        return ExceptionContinueSearch;

    serial_printf("[SEH] __CxxFrameHandler3: C++ exception code=0x%x\n",
                  er->ExceptionCode);
    return ExceptionContinueSearch;
}

/* _CxxThrowException — throw a C++ exception (maps to RaiseException) */
static void shim__CxxThrowException(void *object, void *throw_info) {
    DWORD args[3];
    args[0] = 0x19930520;         /* MSVC magic number */
    args[1] = (DWORD)object;
    args[2] = (DWORD)throw_info;
    seh_RaiseException(EXCEPTION_MSVC_CPP, EXCEPTION_NONCONTINUABLE, 3, args);
}

/* ── Export Table ─────────────────────────────────────────────── */

static const win32_export_entry_t msvcrt_exports[] = {
    /* I/O */
    { "printf",         (void *)shim_printf },
    { "puts",           (void *)shim_puts },
    { "putchar",        (void *)shim_putchar },
    { "sprintf",        (void *)shim_sprintf },
    { "_snprintf",      (void *)shim_snprintf },
    { "snprintf",       (void *)shim_snprintf },
    { "fprintf",        (void *)shim_fprintf },
    { "sscanf",         (void *)shim_sscanf },

    /* Memory */
    { "malloc",         (void *)malloc },
    { "free",           (void *)free },
    { "calloc",         (void *)calloc },
    { "realloc",        (void *)realloc },

    /* String */
    { "strlen",         (void *)strlen },
    { "strcpy",         (void *)strcpy },
    { "strncpy",        (void *)strncpy },
    { "strcat",         (void *)strcat },
    { "strcmp",         (void *)strcmp },
    { "strncmp",        (void *)strncmp },
    { "strchr",         (void *)strchr },
    { "strrchr",        (void *)strrchr },
    { "strstr",         (void *)strstr },
    { "strdup",         (void *)strdup },
    { "strtok",         (void *)strtok },

    /* Memory ops */
    { "memcpy",         (void *)memcpy },
    { "memmove",        (void *)memmove },
    { "memset",         (void *)memset },
    { "memcmp",         (void *)memcmp },

    /* Conversion */
    { "atoi",           (void *)atoi },
    { "strtol",         (void *)strtol },

    /* Math */
    { "abs",            (void *)shim_abs },

    /* ctype */
    { "isdigit",        (void *)shim_isdigit },
    { "isalpha",        (void *)shim_isalpha },
    { "isalnum",        (void *)shim_isalnum },
    { "isspace",        (void *)shim_isspace },
    { "toupper",        (void *)shim_toupper },
    { "tolower",        (void *)shim_tolower },

    /* Process */
    { "exit",           (void *)shim_exit },
    { "_exit",          (void *)shim_exit },
    { "abort",          (void *)shim_abort },

    /* MSVC CRT init */
    { "_initterm",      (void *)shim__initterm },
    { "_initterm_e",    (void *)shim__initterm_e },
    { "__p___argc",     (void *)shim___p___argc },
    { "__p___argv",     (void *)shim___p___argv },
    { "_set_app_type",  (void *)shim__set_app_type },
    { "__set_app_type", (void *)shim___set_app_type },
    { "_get_initial_narrow_environment", (void *)shim__get_initial_narrow_environment },
    { "_configure_narrow_argv",          (void *)shim__configure_narrow_argv },
    { "_initialize_narrow_environment",  (void *)shim__initialize_narrow_environment },
    { "_crt_atexit",    (void *)shim__crt_atexit },
    { "_cexit",         (void *)shim__cexit },
    { "_c_exit",        (void *)shim__c_exit },
    { "_controlfp_s",   (void *)shim__controlfp_s },
    { "__acrt_iob_func", (void *)shim___acrt_iob_func },
    { "_register_onexit_function", (void *)shim__register_onexit_function },
    { "_crt_at_quick_exit", (void *)shim__crt_at_quick_exit },
    { "_errno",         (void *)shim__errno },

    /* Sort */
    { "qsort",          (void *)qsort },
    { "bsearch",        (void *)bsearch },

    /* Random */
    { "rand",           (void *)rand },
    { "srand",          (void *)srand },

    /* Threading */
    { "_beginthreadex", (void *)shim__beginthreadex },
    { "_endthreadex",   (void *)shim__endthreadex },

    /* Delay-load */
    { "__delayLoadHelper2", (void *)shim___delayLoadHelper2 },

    /* Security cookie */
    { "__security_cookie",      (void *)&__security_cookie },
    { "__security_init_cookie", (void *)shim___security_init_cookie },
    { "__report_gsfailure",     (void *)shim___report_gsfailure },
    { "@__security_check_cookie@4", (void *)shim___security_init_cookie }, /* stdcall alias — no-op check */

    /* SEH / C++ exception handlers */
    { "_except_handler3",       (void *)shim__except_handler3 },
    { "_except_handler4",       (void *)shim__except_handler4 },
    { "__CxxFrameHandler3",     (void *)shim___CxxFrameHandler3 },
    { "_CxxThrowException",     (void *)shim__CxxThrowException },

    /* Wide string functions */
    { "wcslen",         (void *)shim_wcslen },
    { "wcscpy",         (void *)shim_wcscpy },
    { "wcsncpy",        (void *)shim_wcsncpy },
    { "wcscat",         (void *)shim_wcscat },
    { "wcscmp",         (void *)shim_wcscmp },
    { "wcsncmp",        (void *)shim_wcsncmp },
    { "wcschr",         (void *)shim_wcschr },
    { "wcsrchr",        (void *)shim_wcsrchr },
    { "wcsstr",         (void *)shim_wcsstr },
    { "_wcsdup",        (void *)shim__wcsdup },
    { "wprintf",        (void *)shim_wprintf },
    { "swprintf",       (void *)shim_swprintf },
    { "_wtoi",          (void *)shim__wtoi },
    { "_wfopen",        (void *)shim__wfopen },
    { "towupper",       (void *)shim_towupper },
    { "towlower",       (void *)shim_towlower },
};

const win32_dll_shim_t win32_msvcrt = {
    .dll_name = "msvcrt.dll",
    .exports = msvcrt_exports,
    .num_exports = sizeof(msvcrt_exports) / sizeof(msvcrt_exports[0]),
};

/* Also register under "api-ms-win-crt-*" aliases — modern MSVC uses these */
const win32_dll_shim_t win32_ucrtbase = {
    .dll_name = "ucrtbase.dll",
    .exports = msvcrt_exports,
    .num_exports = sizeof(msvcrt_exports) / sizeof(msvcrt_exports[0]),
};
