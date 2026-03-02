#include <kernel/win32_types.h>
#include <kernel/win32_seh.h>
#include <kernel/task.h>
#include <kernel/io.h>
#include <kernel/env.h>
#include <kernel/signal.h>
#include <kernel/rtc.h>
#include <kernel/idt.h>
#include <kernel/fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

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
    /* Latin-1 Supplement: U+00E0-U+00FE → U+00C0-U+00DE, excluding U+00F7 (÷) */
    if (c >= 0x00E0 && c <= 0x00FE && c != 0x00F7) return c - 0x20;
    return c;
}

static WCHAR shim_towlower(WCHAR c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    /* Latin-1 Supplement: U+00C0-U+00DE → U+00E0-U+00FE, excluding U+00D7 (×) */
    if (c >= 0x00C0 && c <= 0x00DE && c != 0x00D7) return c + 0x20;
    return c;
}

/* ── Wide string additional functions ────────────────────────── */

static long shim_wcstol(const WCHAR *s, WCHAR **endptr, int base) {
    if (!s) { if (endptr) *endptr = NULL; return 0; }
    while (*s == ' ' || *s == '\t') s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (s[0] == '0') { base = 8; s++; }
        else base = 10;
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    long val = 0;
    while (*s) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        val = val * base + d;
        s++;
    }
    if (endptr) *endptr = (WCHAR *)s;
    return neg ? -val : val;
}

static unsigned long shim_wcstoul(const WCHAR *s, WCHAR **endptr, int base) {
    return (unsigned long)shim_wcstol(s, endptr, base);
}

static int shim__wcsicmp(const WCHAR *a, const WCHAR *b) {
    while (*a && *b) {
        WCHAR ca = shim_towlower(*a);
        WCHAR cb = shim_towlower(*b);
        if (ca != cb) return (int)ca - (int)cb;
        a++; b++;
    }
    return (int)shim_towlower(*a) - (int)shim_towlower(*b);
}

static int shim__wcsnicmp(const WCHAR *a, const WCHAR *b, size_t n) {
    for (size_t i = 0; i < n && *a && *b; i++, a++, b++) {
        WCHAR ca = shim_towlower(*a);
        WCHAR cb = shim_towlower(*b);
        if (ca != cb) return (int)ca - (int)cb;
    }
    return 0;
}

static size_t shim_wcstombs(char *dst, const WCHAR *src, size_t n) {
    extern int win32_wchar_to_utf8(const WCHAR *, int, char *, int);
    return (size_t)win32_wchar_to_utf8(src, -1, dst, (int)n) - 1;
}

static size_t shim_mbstowcs(WCHAR *dst, const char *src, size_t n) {
    extern int win32_utf8_to_wchar(const char *, int, WCHAR *, int);
    return (size_t)win32_utf8_to_wchar(src, -1, dst, (int)n) - 1;
}

static WCHAR *shim__wcslwr(WCHAR *s) {
    for (WCHAR *p = s; *p; p++)
        *p = shim_towlower(*p);
    return s;
}

static WCHAR *shim__wcsupr(WCHAR *s) {
    for (WCHAR *p = s; *p; p++)
        *p = shim_towupper(*p);
    return s;
}

/* ── isw* family ─────────────────────────────────────────────── */

static int shim_iswalpha(WCHAR c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return 1;
    if (c >= 0x00C0 && c <= 0x00FF && c != 0x00D7 && c != 0x00F7) return 1;
    return 0;
}

static int shim_iswdigit(WCHAR c) { return c >= '0' && c <= '9'; }

static int shim_iswalnum(WCHAR c) { return shim_iswalpha(c) || shim_iswdigit(c); }

static int shim_iswspace(WCHAR c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static int shim_iswupper(WCHAR c) {
    if (c >= 'A' && c <= 'Z') return 1;
    if (c >= 0x00C0 && c <= 0x00DE && c != 0x00D7) return 1;
    return 0;
}

static int shim_iswlower(WCHAR c) {
    if (c >= 'a' && c <= 'z') return 1;
    if (c >= 0x00E0 && c <= 0x00FE && c != 0x00F7) return 1;
    return 0;
}

static int shim_iswprint(WCHAR c) {
    return c >= 0x20 && c != 0x7F;
}

static int shim_iswascii(WCHAR c) { return c < 0x80; }

static int shim_iswxdigit(WCHAR c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
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

/* ── C++ Exception Structures (MSVC ABI) ────────────────────── */

/* type_info — MSVC RTTI type descriptor */
typedef struct {
    void  *pVFTable;       /* vtable pointer (points to type_info vtable) */
    void  *spare;          /* internal use */
    char   name[1];        /* decorated type name (variable length) */
} msvc_type_info_t;

/* CatchableType — describes one type a thrown object can be caught as */
typedef struct {
    uint32_t  properties;      /* 0x01 = simple type, 0x02 = can be caught by ref */
    msvc_type_info_t *pType;   /* pointer to type_info for this type */
    int32_t   thisDisplacement[3]; /* PMD: mdisp, pdisp, vdisp */
    int32_t   sizeOrOffset;    /* sizeof(type) for simple types */
    void     *copyFunction;    /* copy constructor (NULL for simple types) */
} CatchableType;

/* CatchableTypeArray — list of types a thrown object can be caught as */
typedef struct {
    int32_t        nCatchableTypes;
    CatchableType *arrayOfCatchableTypes[1]; /* variable length */
} CatchableTypeArray;

/* ThrowInfo — describes a thrown C++ exception */
typedef struct {
    uint32_t            attributes;     /* 0x01 = const, 0x02 = volatile */
    void               *pmfnUnwind;     /* destructor for thrown object */
    void               *pForwardCompat; /* forward compat handler */
    CatchableTypeArray *pCatchableTypeArray;
} ThrowInfo;

/* FuncInfo — per-function exception handling data from compiler */
typedef struct {
    uint32_t  magicNumber;      /* 0x19930520 = VC7, 0x19930521 = VC8 */
    int32_t   maxState;         /* max unwind state */
    void     *pUnwindMap;       /* unwind map entries */
    uint32_t  nTryBlocks;       /* number of try blocks */
    void     *pTryBlockMap;     /* try block map entries */
    uint32_t  nIPMapEntries;
    void     *pIPtoStateMap;
    void     *pESTypeList;      /* VC8 only */
    int32_t   EHFlags;          /* VC8 only */
} FuncInfo;

/* TryBlockMapEntry */
typedef struct {
    int32_t  tryLow;
    int32_t  tryHigh;
    int32_t  catchHigh;
    int32_t  nCatches;
    void    *pHandlerArray;  /* HandlerType array */
} TryBlockMapEntry;

/* HandlerType — describes one catch clause */
typedef struct {
    uint32_t          adjectives;  /* 0x01 = const, 0x02 = volatile, 0x08 = reference */
    msvc_type_info_t *pType;       /* type_info for this catch (NULL = catch(...)) */
    int32_t           dispCatchObj;/* displacement of catch object on stack */
    void             *addressOfHandler; /* address of catch block code */
} HandlerType;

/* UnwindMapEntry — one entry in the destructor unwind table */
typedef struct {
    int32_t  toState;       /* state to transition to (-1 = base) */
    void    *action;        /* destructor to call */
} UnwindMapEntry;

/* __CxxFrameHandler3 — C++ exception handler with proper type matching */
static EXCEPTION_DISPOSITION shim___CxxFrameHandler3(
    EXCEPTION_RECORD *er, void *frame,
    CONTEXT *ctx, void *dispatcher_ctx)
{
    (void)ctx; (void)dispatcher_ctx;

    /* During unwind, call destructors via unwind map */
    if (er->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND)) {
        /* Get FuncInfo from the registration frame.
         * MSVC stores it at [frame + 8] (after Next and Handler). */
        uint32_t *fp = (uint32_t *)frame;
        FuncInfo *func_info = (FuncInfo *)fp[2];
        if (func_info && func_info->pUnwindMap) {
            int32_t cur_state = (int32_t)fp[3]; /* trylevel */
            UnwindMapEntry *map = (UnwindMapEntry *)func_info->pUnwindMap;
            while (cur_state >= 0 && cur_state < func_info->maxState) {
                UnwindMapEntry *entry = &map[cur_state];
                if (entry->action) {
                    /* Call destructor: void __cdecl action(void) */
                    typedef void (*dtor_fn_t)(void);
                    dtor_fn_t dtor = (dtor_fn_t)entry->action;
                    serial_printf("[C++] calling dtor at state %d\n", cur_state);
                    dtor();
                }
                cur_state = entry->toState;
            }
        }
        return ExceptionContinueSearch;
    }

    /* Only handle MSVC C++ exceptions */
    if (er->ExceptionCode != EXCEPTION_MSVC_CPP) {
        serial_printf("[C++] __CxxFrameHandler3: non-C++ exception 0x%x\n",
                      er->ExceptionCode);
        return ExceptionContinueSearch;
    }

    /* Validate MSVC magic */
    if (er->NumberParameters < 3 || er->ExceptionInformation[0] != 0x19930520) {
        serial_printf("[C++] __CxxFrameHandler3: bad magic\n");
        return ExceptionContinueSearch;
    }

    /* Extract throw info */
    void *thrown_object = (void *)er->ExceptionInformation[1];
    ThrowInfo *throw_info = (ThrowInfo *)er->ExceptionInformation[2];

    if (!throw_info || !throw_info->pCatchableTypeArray) {
        serial_printf("[C++] __CxxFrameHandler3: no throw info\n");
        return ExceptionContinueSearch;
    }

    /* Get FuncInfo from the frame */
    uint32_t *fp = (uint32_t *)frame;
    FuncInfo *func_info = (FuncInfo *)fp[2];
    if (!func_info) return ExceptionContinueSearch;

    int32_t cur_state = (int32_t)fp[3];

    /* Walk try blocks looking for a matching catch */
    TryBlockMapEntry *try_map = (TryBlockMapEntry *)func_info->pTryBlockMap;
    if (!try_map) return ExceptionContinueSearch;

    for (uint32_t i = 0; i < func_info->nTryBlocks; i++) {
        TryBlockMapEntry *tb = &try_map[i];
        if (cur_state < tb->tryLow || cur_state > tb->tryHigh)
            continue;

        /* Check each catch handler in this try block */
        HandlerType *handlers = (HandlerType *)tb->pHandlerArray;
        if (!handlers) continue;

        for (int32_t j = 0; j < tb->nCatches; j++) {
            HandlerType *ht = &handlers[j];

            /* catch(...) — NULL type matches everything */
            if (ht->pType == NULL) {
                serial_printf("[C++] catch(...) matched at try block %u\n", i);
                /* Copy thrown object if needed */
                if (ht->dispCatchObj && thrown_object) {
                    char *frame_base = (char *)frame;
                    memcpy(frame_base + ht->dispCatchObj, &thrown_object, sizeof(void *));
                }
                /* Unwind to catch state, then jump to handler */
                fp[3] = (uint32_t)tb->catchHigh;
                return ExceptionContinueSearch; /* Let unwind phase handle it */
            }

            /* Type-based matching: compare type_info names */
            CatchableTypeArray *cta = throw_info->pCatchableTypeArray;
            for (int32_t k = 0; k < cta->nCatchableTypes; k++) {
                CatchableType *ct = cta->arrayOfCatchableTypes[k];
                if (!ct || !ct->pType) continue;

                /* Compare decorated type names */
                if (ht->pType && ct->pType &&
                    strcmp(ht->pType->name, ct->pType->name) == 0) {
                    serial_printf("[C++] type match: %s at try block %u catch %d\n",
                                  ht->pType->name, i, j);
                    if (ht->dispCatchObj && thrown_object) {
                        char *frame_base = (char *)frame;
                        memcpy(frame_base + ht->dispCatchObj, &thrown_object,
                               sizeof(void *));
                    }
                    fp[3] = (uint32_t)tb->catchHigh;
                    return ExceptionContinueSearch;
                }
            }
        }
    }

    serial_printf("[C++] __CxxFrameHandler3: no matching catch for exception\n");
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

/* __CppXcptFilter — C++ exception filter used by CRT startup.
 * Returns EXCEPTION_EXECUTE_HANDLER for C++ exceptions,
 * EXCEPTION_CONTINUE_SEARCH for others. */
static int shim___CppXcptFilter(int code, EXCEPTION_POINTERS *ep) {
    (void)ep;
    if (code == (int)EXCEPTION_MSVC_CPP)
        return EXCEPTION_EXECUTE_HANDLER;
    return EXCEPTION_CONTINUE_SEARCH;
}

/* _set_se_translator — set SEH-to-C++ exception translator */
static void *shim__set_se_translator(void *func) {
    return (void *)seh_set_se_translator((_se_translator_function)func);
}

/* ── stdio additions ─────────────────────────────────────────── */

static int shim_fseek(FILE *f, long offset, int whence) {
    return fseek(f, offset, whence);
}

static long shim_ftell(FILE *f) {
    return ftell(f);
}

static void shim_rewind(FILE *f) {
    rewind(f);
}

static int shim_fgetpos(FILE *f, long *pos) {
    if (!f || !pos) return -1;
    *pos = ftell(f);
    return 0;
}

static int shim_fsetpos(FILE *f, const long *pos) {
    if (!f || !pos) return -1;
    return fseek(f, *pos, 0);
}

static void shim_perror(const char *msg) {
    if (msg && msg[0])
        printf("%s: error\n", msg);
    else
        printf("error\n");
}

static int shim_setvbuf(FILE *f, char *buf, int mode, size_t size) {
    (void)f; (void)buf; (void)mode; (void)size;
    return 0; /* no-op */
}

static FILE *shim_tmpfile(void) {
    return fopen("/tmp/tmpXXXXXX", "w+");
}

static int shim_ungetc(int c, FILE *f) {
    return ungetc(c, f);
}

static int shim_vprintf(const char *fmt, __builtin_va_list ap) {
    char buf[512];
    int ret = vsnprintf(buf, sizeof(buf), fmt, ap);
    for (int i = 0; buf[i]; i++) putchar(buf[i]);
    return ret;
}

static int shim_vfprintf(FILE *stream, const char *fmt, __builtin_va_list ap) {
    (void)stream;
    char buf[512];
    int ret = vsnprintf(buf, sizeof(buf), fmt, ap);
    for (int i = 0; buf[i]; i++) putchar(buf[i]);
    return ret;
}

static int shim_vsprintf(char *buf, const char *fmt, __builtin_va_list ap) {
    return vsnprintf(buf, 4096, fmt, ap);
}

static int shim_vsnprintf(char *buf, size_t n, const char *fmt, __builtin_va_list ap) {
    return vsnprintf(buf, n, fmt, ap);
}

static int shim_getc(FILE *f) { return fgetc(f); }
static int shim_putc(int c, FILE *f) { return fputc(c, f); }

static int shim__fileno(FILE *f) {
    (void)f;
    return 3; /* fake fd */
}

static FILE *shim_fopen(const char *path, const char *mode) {
    return fopen(path, mode);
}
static int shim_fclose(FILE *f) { return fclose(f); }
static int shim_fgetc(FILE *f) { return fgetc(f); }
static int shim_fputc(int c, FILE *f) { return fputc(c, f); }
static size_t shim_fread(void *p, size_t s, size_t n, FILE *f) { return fread(p, s, n, f); }
static size_t shim_fwrite(const void *p, size_t s, size_t n, FILE *f) { return fwrite(p, s, n, f); }
static int shim_fflush(FILE *f) { return fflush(f); }
static int shim_feof(FILE *f) { return feof(f); }
static int shim_ferror(FILE *f) { return ferror(f); }
static int shim_fputs(const char *s, FILE *f) { return fputs(s, f); }
static char *shim_fgets(char *s, int n, FILE *f) { return fgets(s, n, f); }

/* ── stdlib additions ────────────────────────────────────────── */

static unsigned long shim_strtoul(const char *s, char **endptr, int base) {
    return strtoul(s, endptr, base);
}

static unsigned long long shim_strtoull(const char *s, char **endptr, int base) {
    return strtoull(s, endptr, base);
}

/* strtod/atof — no FPU, return truncated integer cast */
static long shim_strtod(const char *s, char **endptr) {
    return strtol(s, endptr, 10);
}

static int shim_atof(const char *s) {
    return atoi(s);
}

static const char *shim_getenv(const char *name) {
    return env_get(name);
}

static int shim_putenv(const char *string) {
    /* string is "NAME=VALUE" */
    if (!string) return -1;
    char name[64];
    const char *eq = strchr(string, '=');
    if (!eq) return -1;
    size_t nlen = (size_t)(eq - string);
    if (nlen >= sizeof(name)) nlen = sizeof(name) - 1;
    memcpy(name, string, nlen);
    name[nlen] = '\0';
    return env_set(name, eq + 1);
}

static int shim_system(const char *cmd) {
    (void)cmd;
    return -1;
}

static char *shim__itoa(int value, char *buf, int radix) {
    if (!buf || radix < 2 || radix > 36) return buf;
    char tmp[34];
    int i = 0, neg = 0;
    unsigned int uval;
    if (value < 0 && radix == 10) { neg = 1; uval = (unsigned int)(-value); }
    else uval = (unsigned int)value;
    do { int d = uval % radix; tmp[i++] = d < 10 ? '0' + d : 'a' + d - 10; uval /= radix; } while (uval);
    if (neg) tmp[i++] = '-';
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
    return buf;
}

static char *shim__ltoa(long value, char *buf, int radix) {
    return shim__itoa((int)value, buf, radix);
}

static char *shim__ultoa(unsigned long value, char *buf, int radix) {
    if (!buf || radix < 2 || radix > 36) return buf;
    char tmp[34];
    int i = 0;
    do { int d = value % radix; tmp[i++] = d < 10 ? '0' + d : 'a' + d - 10; value /= radix; } while (value);
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
    return buf;
}

static long long shim__atoi64(const char *s) {
    return (long long)strtol(s, NULL, 10);
}

static long long shim__abs64(long long v) {
    return v < 0 ? -v : v;
}

static long long shim_llabs(long long v) {
    return v < 0 ? -v : v;
}

/* ── string additions ────────────────────────────────────────── */

static int shim__stricmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        int cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
    int cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
    return ca - cb;
}

static int shim__strnicmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n && *a && *b; i++, a++, b++) {
        int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        int cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb) return ca - cb;
    }
    return 0;
}

static char *shim__strdup(const char *s) {
    return strdup(s);
}

static char *shim_strncat(char *dst, const char *src, size_t n) {
    return strncat(dst, src, n);
}

static const char *shim_strerror(int errnum) {
    (void)errnum;
    return "Unknown error";
}

static char *shim__strlwr(char *s) {
    for (char *p = s; *p; p++)
        if (*p >= 'A' && *p <= 'Z') *p += 32;
    return s;
}

static char *shim__strupr(char *s) {
    for (char *p = s; *p; p++)
        if (*p >= 'a' && *p <= 'z') *p -= 32;
    return s;
}

/* ── time.h ──────────────────────────────────────────────────── */

typedef uint32_t msvcrt_time_t;
typedef uint32_t msvcrt_clock_t;

typedef struct {
    int tm_sec;    /* 0-59 */
    int tm_min;    /* 0-59 */
    int tm_hour;   /* 0-23 */
    int tm_mday;   /* 1-31 */
    int tm_mon;    /* 0-11 */
    int tm_year;   /* years since 1900 */
    int tm_wday;   /* 0-6, Sunday=0 */
    int tm_yday;   /* 0-365 */
    int tm_isdst;  /* DST flag */
} msvcrt_tm_t;

/* Seconds between 1970-01-01 and 2000-01-01 */
#define EPOCH_2000_OFFSET 946684800U

static msvcrt_tm_t _static_tm;

static const int _mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};

static int _is_leap(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static msvcrt_time_t shim_time(msvcrt_time_t *t) {
    uint32_t epoch2000 = rtc_get_epoch();
    msvcrt_time_t unix_time = epoch2000 + EPOCH_2000_OFFSET;
    if (t) *t = unix_time;
    return unix_time;
}

static msvcrt_tm_t *shim_localtime(const msvcrt_time_t *t) {
    if (!t) return NULL;
    uint32_t epoch2000 = *t - EPOCH_2000_OFFSET;
    datetime_t dt;
    epoch_to_datetime(epoch2000, &dt);
    _static_tm.tm_sec = dt.second;
    _static_tm.tm_min = dt.minute;
    _static_tm.tm_hour = dt.hour;
    _static_tm.tm_mday = dt.day;
    _static_tm.tm_mon = dt.month - 1;
    _static_tm.tm_year = dt.year - 1900;
    _static_tm.tm_wday = 0; /* approximate */
    _static_tm.tm_yday = 0;
    _static_tm.tm_isdst = 0;
    return &_static_tm;
}

static msvcrt_tm_t *shim_gmtime(const msvcrt_time_t *t) {
    return shim_localtime(t); /* no TZ distinction */
}

static msvcrt_time_t shim_mktime(msvcrt_tm_t *tm) {
    if (!tm) return (msvcrt_time_t)-1;
    int year = tm->tm_year + 1900;
    int mon = tm->tm_mon; /* 0-11 */
    int day = tm->tm_mday;
    /* Count days from 1970-01-01 */
    uint32_t days = 0;
    for (int y = 1970; y < year; y++)
        days += _is_leap(y) ? 366 : 365;
    for (int m = 0; m < mon; m++) {
        days += _mdays[m];
        if (m == 1 && _is_leap(year)) days++;
    }
    days += day - 1;
    return days * 86400 + tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec;
}

static const char *_wday_names[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
static const char *_mon_names[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
static const char *_wday_full[] = {"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
static const char *_mon_full[] = {"January","February","March","April","May","June","July","August","September","October","November","December"};

static size_t shim_strftime(char *buf, size_t max, const char *fmt, const msvcrt_tm_t *tm) {
    if (!buf || max == 0 || !fmt || !tm) return 0;
    size_t pos = 0;
    while (*fmt && pos < max - 1) {
        if (*fmt == '%') {
            fmt++;
            char tmp[32];
            const char *s = tmp;
            switch (*fmt) {
                case 'Y': snprintf(tmp, sizeof(tmp), "%04d", tm->tm_year + 1900); break;
                case 'm': snprintf(tmp, sizeof(tmp), "%02d", tm->tm_mon + 1); break;
                case 'd': snprintf(tmp, sizeof(tmp), "%02d", tm->tm_mday); break;
                case 'H': snprintf(tmp, sizeof(tmp), "%02d", tm->tm_hour); break;
                case 'M': snprintf(tmp, sizeof(tmp), "%02d", tm->tm_min); break;
                case 'S': snprintf(tmp, sizeof(tmp), "%02d", tm->tm_sec); break;
                case 'A': s = (tm->tm_wday >= 0 && tm->tm_wday < 7) ? _wday_full[tm->tm_wday] : "???"; break;
                case 'a': s = (tm->tm_wday >= 0 && tm->tm_wday < 7) ? _wday_names[tm->tm_wday] : "???"; break;
                case 'B': s = (tm->tm_mon >= 0 && tm->tm_mon < 12) ? _mon_full[tm->tm_mon] : "???"; break;
                case 'b': s = (tm->tm_mon >= 0 && tm->tm_mon < 12) ? _mon_names[tm->tm_mon] : "???"; break;
                case 'p': s = tm->tm_hour >= 12 ? "PM" : "AM"; break;
                case '%': s = "%"; break;
                default: tmp[0] = '%'; tmp[1] = *fmt; tmp[2] = '\0'; break;
            }
            while (*s && pos < max - 1) buf[pos++] = *s++;
            fmt++;
        } else {
            buf[pos++] = *fmt++;
        }
    }
    buf[pos] = '\0';
    return pos;
}

static long shim_difftime(msvcrt_time_t t1, msvcrt_time_t t0) {
    return (long)(t1 - t0);
}

static msvcrt_clock_t shim_clock(void) {
    return (msvcrt_clock_t)pit_get_ticks();
}

typedef struct {
    msvcrt_time_t time;
    unsigned short millitm;
    short timezone;
    short dstflag;
} msvcrt_timeb_t;

static void shim__ftime(msvcrt_timeb_t *tb) {
    if (!tb) return;
    tb->time = shim_time(NULL);
    tb->millitm = 0;
    tb->timezone = 0;
    tb->dstflag = 0;
}

/* ── math.h stubs ────────────────────────────────────────────── */
/* No FPU — integer approximations or stubs to prevent link failures */

static int shim_fabs(int x) { return x < 0 ? -x : x; }
static int shim_floor(int x) { return x; } /* integer is already floored */
static int shim_ceil(int x) { return x; }
static int shim_fmod(int x, int y) { return y ? x % y : 0; }

/* Newton's method integer sqrt */
static int shim_sqrt(int x) {
    if (x <= 0) return 0;
    int r = x, prev;
    do { prev = r; r = (r + x / r) / 2; } while (r < prev);
    return prev;
}

/* Trig stubs — return 0 */
static int shim_sin(int x) { (void)x; return 0; }
static int shim_cos(int x) { (void)x; return 0; }
static int shim_tan(int x) { (void)x; return 0; }
static int shim_atan2(int y, int x) { (void)y; (void)x; return 0; }
static int shim_asin(int x) { (void)x; return 0; }
static int shim_acos(int x) { (void)x; return 0; }

/* pow — integer power for simple cases */
static int shim_pow(int base_val, int exp_val) {
    if (exp_val < 0) return 0;
    int result = 1;
    for (int i = 0; i < exp_val && i < 31; i++) result *= base_val;
    return result;
}

static int shim_exp(int x) { (void)x; return 1; }
static int shim_log(int x) { (void)x; return 0; }
static int shim_log10(int x) { (void)x; return 0; }

/* ── signal.h (C runtime) ───────────────────────────────────── */

typedef void (*msvcrt_sig_handler_t)(int);

static msvcrt_sig_handler_t shim_signal(int sig, msvcrt_sig_handler_t handler) {
    extern int task_get_current(void);
    int tid = task_get_current();
    sig_handler_t prev = sig_set_handler(tid, sig, (sig_handler_t)handler);
    return (msvcrt_sig_handler_t)prev;
}

static int shim_raise(int sig) {
    extern int task_get_current(void);
    int tid = task_get_current();
    return sig_send(tid, sig);
}

/* ── locale.h ────────────────────────────────────────────────── */

typedef struct {
    char *decimal_point;
    char *thousands_sep;
    char *grouping;
    char *int_curr_symbol;
    char *currency_symbol;
    char *mon_decimal_point;
    char *mon_thousands_sep;
    char *mon_grouping;
    char *positive_sign;
    char *negative_sign;
} msvcrt_lconv_t;

static char _c_decimal[] = ".";
static char _c_empty[] = "";

static msvcrt_lconv_t _static_lconv = {
    .decimal_point = _c_decimal,
    .thousands_sep = _c_empty,
    .grouping = _c_empty,
    .int_curr_symbol = _c_empty,
    .currency_symbol = _c_empty,
    .mon_decimal_point = _c_empty,
    .mon_thousands_sep = _c_empty,
    .mon_grouping = _c_empty,
    .positive_sign = _c_empty,
    .negative_sign = _c_empty,
};

static const char *shim_setlocale(int category, const char *locale) {
    (void)category; (void)locale;
    return "C";
}

static msvcrt_lconv_t *shim_localeconv(void) {
    return &_static_lconv;
}

/* ── ctype completions ───────────────────────────────────────── */

static int shim_isupper(int c) { return c >= 'A' && c <= 'Z'; }
static int shim_islower(int c) { return c >= 'a' && c <= 'z'; }
static int shim_isprint(int c) { return c >= 0x20 && c <= 0x7E; }
static int shim_iscntrl(int c) { return (c >= 0 && c < 0x20) || c == 0x7F; }
static int shim_ispunct(int c) { return shim_isprint(c) && !shim_isalnum(c) && c != ' '; }
static int shim_isgraph(int c) { return c > 0x20 && c <= 0x7E; }
static int shim_isxdigit(int c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

/* ── POSIX-style I/O ────────────────────────────────────────── */

#define MSVCRT_MAX_FD 16
static FILE *_fd_table[MSVCRT_MAX_FD];
static int _fd_inited = 0;

static void _fd_init(void) {
    if (_fd_inited) return;
    _fd_table[0] = stdin;
    _fd_table[1] = stdout;
    _fd_table[2] = stderr;
    for (int i = 3; i < MSVCRT_MAX_FD; i++) _fd_table[i] = NULL;
    _fd_inited = 1;
}

static int _fd_alloc(FILE *f) {
    _fd_init();
    for (int i = 3; i < MSVCRT_MAX_FD; i++) {
        if (!_fd_table[i]) { _fd_table[i] = f; return i; }
    }
    return -1;
}

static int shim__open(const char *path, int flags, int mode) {
    (void)flags; (void)mode;
    _fd_init();
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    return _fd_alloc(f);
}

static int shim__read(int fd, void *buf, unsigned int count) {
    _fd_init();
    if (fd < 0 || fd >= MSVCRT_MAX_FD || !_fd_table[fd]) return -1;
    return (int)fread(buf, 1, count, _fd_table[fd]);
}

static int shim__write(int fd, const void *buf, unsigned int count) {
    _fd_init();
    if (fd < 0 || fd >= MSVCRT_MAX_FD || !_fd_table[fd]) return -1;
    return (int)fwrite(buf, 1, count, _fd_table[fd]);
}

static int shim__close(int fd) {
    _fd_init();
    if (fd < 3 || fd >= MSVCRT_MAX_FD || !_fd_table[fd]) return -1;
    fclose(_fd_table[fd]);
    _fd_table[fd] = NULL;
    return 0;
}

static long shim__lseek(int fd, long offset, int origin) {
    _fd_init();
    if (fd < 0 || fd >= MSVCRT_MAX_FD || !_fd_table[fd]) return -1;
    if (fseek(_fd_table[fd], offset, origin) != 0) return -1;
    return ftell(_fd_table[fd]);
}

/* ── _stat / _fstat / _access ────────────────────────────────── */

typedef struct {
    uint32_t st_dev;
    uint32_t st_ino;
    uint16_t st_mode;
    uint16_t st_nlink;
    uint16_t st_uid;
    uint16_t st_gid;
    uint32_t st_rdev;
    uint32_t st_size;
    uint32_t st_atime;
    uint32_t st_mtime;
    uint32_t st_ctime;
} msvcrt_stat_t;

static int shim__stat(const char *path, msvcrt_stat_t *buf) {
    if (!path || !buf) return -1;
    memset(buf, 0, sizeof(*buf));
    uint32_t parent;
    char name[28];
    int ino = fs_resolve_path(path, &parent, name);
    if (ino < 0) return -1;
    inode_t node;
    fs_read_inode(ino, &node);
    buf->st_size = node.size;
    buf->st_mode = (node.type == 2) ? 0040755 : 0100644;
    buf->st_nlink = 1;
    buf->st_mtime = node.modified_at;
    buf->st_atime = (uint32_t)node.accessed_hi << 16;
    buf->st_ctime = node.created_at;
    return 0;
}

static int shim__fstat(int fd, msvcrt_stat_t *buf) {
    _fd_init();
    if (fd < 0 || fd >= MSVCRT_MAX_FD || !buf) return -1;
    memset(buf, 0, sizeof(*buf));
    /* For std streams, return minimal info */
    if (fd < 3) {
        buf->st_mode = 0020666; /* char device */
        return 0;
    }
    if (!_fd_table[fd]) return -1;
    buf->st_size = 0;
    buf->st_mode = 0100644;
    return 0;
}

static int shim__access(const char *path, int mode) {
    (void)mode;
    if (!path) return -1;
    uint32_t parent;
    char name[28];
    int ino = fs_resolve_path(path, &parent, name);
    return ino >= 0 ? 0 : -1;
}

/* ── msvcrt global state ─────────────────────────────────────── */

static char _acmdln_buf[1] = "";
static char _pgmptr_buf[1] = "";
static char *_shim_acmdln = _acmdln_buf;
static char *_shim_pgmptr = _pgmptr_buf;
static int _shim___argc = 0;
static char **_shim___argv = NULL;
static char *_shim_environ = NULL;

/* ── C++ operator new / delete ───────────────────────────────── */

static void *shim_operator_new(size_t size) {
    if (size == 0) size = 1;
    return malloc(size);
}

static void *shim_operator_new_array(size_t size) {
    if (size == 0) size = 1;
    return malloc(size);
}

static void shim_operator_delete(void *ptr) {
    free(ptr);
}

static void shim_operator_delete_array(void *ptr) {
    free(ptr);
}

/* ── C++ RTTI stubs ──────────────────────────────────────────── */

static void *shim___RTtypeid(void *obj) {
    (void)obj;
    return NULL;
}

static void *shim___RTDynamicCast(void *obj, int vfdelta, void *srctype, void *dsttype, int isref) {
    (void)obj; (void)vfdelta; (void)srctype; (void)dsttype; (void)isref;
    return NULL;
}

/* Fake type_info vtable */
static void *_fake_type_info_vtable[4] = {NULL, NULL, NULL, NULL};

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
    { "__CppXcptFilter",        (void *)shim___CppXcptFilter },
    { "_set_se_translator",     (void *)shim__set_se_translator },

    /* stdio additions */
    { "fopen",          (void *)shim_fopen },
    { "fclose",         (void *)shim_fclose },
    { "fgetc",          (void *)shim_fgetc },
    { "fputc",          (void *)shim_fputc },
    { "fread",          (void *)shim_fread },
    { "fwrite",         (void *)shim_fwrite },
    { "fflush",         (void *)shim_fflush },
    { "feof",           (void *)shim_feof },
    { "ferror",         (void *)shim_ferror },
    { "fputs",          (void *)shim_fputs },
    { "fgets",          (void *)shim_fgets },
    { "fseek",          (void *)shim_fseek },
    { "ftell",          (void *)shim_ftell },
    { "rewind",         (void *)shim_rewind },
    { "fgetpos",        (void *)shim_fgetpos },
    { "fsetpos",        (void *)shim_fsetpos },
    { "perror",         (void *)shim_perror },
    { "setvbuf",        (void *)shim_setvbuf },
    { "tmpfile",        (void *)shim_tmpfile },
    { "ungetc",         (void *)shim_ungetc },
    { "vprintf",        (void *)shim_vprintf },
    { "vfprintf",       (void *)shim_vfprintf },
    { "vsprintf",       (void *)shim_vsprintf },
    { "vsnprintf",      (void *)shim_vsnprintf },
    { "_vsnprintf",     (void *)shim_vsnprintf },
    { "getc",           (void *)shim_getc },
    { "putc",           (void *)shim_putc },
    { "_fileno",        (void *)shim__fileno },

    /* stdlib additions */
    { "strtoul",        (void *)shim_strtoul },
    { "strtoull",       (void *)shim_strtoull },
    { "strtod",         (void *)shim_strtod },
    { "atof",           (void *)shim_atof },
    { "getenv",         (void *)shim_getenv },
    { "putenv",         (void *)shim_putenv },
    { "_putenv",        (void *)shim_putenv },
    { "system",         (void *)shim_system },
    { "_itoa",          (void *)shim__itoa },
    { "_ltoa",          (void *)shim__ltoa },
    { "_ultoa",         (void *)shim__ultoa },
    { "_atoi64",        (void *)shim__atoi64 },
    { "_abs64",         (void *)shim__abs64 },
    { "llabs",          (void *)shim_llabs },
    { "labs",           (void *)labs },

    /* string additions */
    { "_stricmp",       (void *)shim__stricmp },
    { "_strnicmp",      (void *)shim__strnicmp },
    { "_strdup",        (void *)shim__strdup },
    { "strncat",        (void *)shim_strncat },
    { "strerror",       (void *)shim_strerror },
    { "_strlwr",        (void *)shim__strlwr },
    { "_strupr",        (void *)shim__strupr },

    /* time */
    { "time",           (void *)shim_time },
    { "localtime",      (void *)shim_localtime },
    { "gmtime",         (void *)shim_gmtime },
    { "mktime",         (void *)shim_mktime },
    { "strftime",       (void *)shim_strftime },
    { "difftime",       (void *)shim_difftime },
    { "clock",          (void *)shim_clock },
    { "_ftime",         (void *)shim__ftime },
    { "_ftime64",       (void *)shim__ftime },

    /* math stubs */
    { "fabs",           (void *)shim_fabs },
    { "floor",          (void *)shim_floor },
    { "ceil",           (void *)shim_ceil },
    { "fmod",           (void *)shim_fmod },
    { "sqrt",           (void *)shim_sqrt },
    { "sin",            (void *)shim_sin },
    { "cos",            (void *)shim_cos },
    { "tan",            (void *)shim_tan },
    { "atan2",          (void *)shim_atan2 },
    { "asin",           (void *)shim_asin },
    { "acos",           (void *)shim_acos },
    { "pow",            (void *)shim_pow },
    { "exp",            (void *)shim_exp },
    { "log",            (void *)shim_log },
    { "log10",          (void *)shim_log10 },

    /* signal */
    { "signal",         (void *)shim_signal },
    { "raise",          (void *)shim_raise },

    /* locale */
    { "setlocale",      (void *)shim_setlocale },
    { "localeconv",     (void *)shim_localeconv },

    /* ctype completions */
    { "isupper",        (void *)shim_isupper },
    { "islower",        (void *)shim_islower },
    { "isprint",        (void *)shim_isprint },
    { "iscntrl",        (void *)shim_iscntrl },
    { "ispunct",        (void *)shim_ispunct },
    { "isgraph",        (void *)shim_isgraph },
    { "isxdigit",       (void *)shim_isxdigit },

    /* POSIX-style I/O */
    { "_open",          (void *)shim__open },
    { "_read",          (void *)shim__read },
    { "_write",         (void *)shim__write },
    { "_close",         (void *)shim__close },
    { "_lseek",         (void *)shim__lseek },

    /* stat / access */
    { "_stat",          (void *)shim__stat },
    { "_fstat",         (void *)shim__fstat },
    { "_access",        (void *)shim__access },

    /* msvcrt global state */
    { "_acmdln",        (void *)&_shim_acmdln },
    { "_pgmptr",        (void *)&_shim_pgmptr },
    { "__argc",         (void *)&_shim___argc },
    { "__argv",         (void *)&_shim___argv },
    { "_environ",       (void *)&_shim_environ },

    /* C++ operator new/delete (MSVC mangled names) */
    { "??2@YAPAXI@Z",  (void *)shim_operator_new },
    { "??_U@YAPAXI@Z", (void *)shim_operator_new_array },
    { "??3@YAXPAX@Z",  (void *)shim_operator_delete },
    { "??_V@YAXPAX@Z", (void *)shim_operator_delete_array },

    /* C++ RTTI stubs */
    { "__RTtypeid",     (void *)shim___RTtypeid },
    { "__RTDynamicCast", (void *)shim___RTDynamicCast },
    { "??_7type_info@@6B@", (void *)_fake_type_info_vtable },

    /* setjmp/longjmp */
    { "setjmp",         (void *)setjmp },
    { "longjmp",        (void *)longjmp },
    { "_setjmp",        (void *)setjmp },
    { "_longjmp",       (void *)longjmp },

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

    /* Wide string additions */
    { "wcstol",         (void *)shim_wcstol },
    { "wcstoul",        (void *)shim_wcstoul },
    { "_wcsicmp",       (void *)shim__wcsicmp },
    { "_wcsnicmp",      (void *)shim__wcsnicmp },
    { "wcstombs",       (void *)shim_wcstombs },
    { "mbstowcs",       (void *)shim_mbstowcs },
    { "_wcslwr",        (void *)shim__wcslwr },
    { "_wcsupr",        (void *)shim__wcsupr },

    /* isw* family */
    { "iswalpha",       (void *)shim_iswalpha },
    { "iswdigit",       (void *)shim_iswdigit },
    { "iswalnum",       (void *)shim_iswalnum },
    { "iswspace",       (void *)shim_iswspace },
    { "iswupper",       (void *)shim_iswupper },
    { "iswlower",       (void *)shim_iswlower },
    { "iswprint",       (void *)shim_iswprint },
    { "iswascii",       (void *)shim_iswascii },
    { "iswxdigit",      (void *)shim_iswxdigit },
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
