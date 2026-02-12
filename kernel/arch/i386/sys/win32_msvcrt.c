#include <kernel/win32_types.h>
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
