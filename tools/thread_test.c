/* Win32 threading test for ImposOS Phase 1 validation.
 *
 * Build with:
 *   i686-w64-mingw32-gcc -o thread_test.exe thread_test.c \
 *       -nostdlib -lkernel32 -luser32 -lgdi32 \
 *       -Wl,--subsystem,console -e _mainCRTStartup
 */
#include <windows.h>

/* We link -nostdlib so we need our own entry point */
void mainCRTStartup(void);

/* ── Test 1: Basic CreateThread ────────────────────────────── */

static DWORD WINAPI thread_func(LPVOID param) {
    int id = (int)(DWORD)param;
    /* Write to console via kernel32 */
    char buf[64];
    DWORD written;
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

    int len = 0;
    buf[len++] = '[';
    buf[len++] = 'T';
    buf[len++] = '0' + (char)id;
    buf[len++] = ']';
    buf[len++] = ' ';
    buf[len++] = 'H';
    buf[len++] = 'e';
    buf[len++] = 'l';
    buf[len++] = 'l';
    buf[len++] = 'o';
    buf[len++] = ' ';
    buf[len++] = 'f';
    buf[len++] = 'r';
    buf[len++] = 'o';
    buf[len++] = 'm';
    buf[len++] = ' ';
    buf[len++] = 't';
    buf[len++] = 'h';
    buf[len++] = 'r';
    buf[len++] = 'e';
    buf[len++] = 'a';
    buf[len++] = 'd';
    buf[len++] = '\n';
    buf[len] = 0;

    WriteFile(hOut, buf, (DWORD)len, &written, NULL);

    /* Sleep a bit to test concurrency */
    Sleep(100);

    return (DWORD)(id * 10);  /* exit code = id*10 */
}

/* ── Test 2: Critical Section ──────────────────────────────── */

static CRITICAL_SECTION cs;
static volatile int shared_counter = 0;

static DWORD WINAPI cs_thread(LPVOID param) {
    (void)param;
    for (int i = 0; i < 100; i++) {
        EnterCriticalSection(&cs);
        shared_counter++;
        LeaveCriticalSection(&cs);
    }
    return 0;
}

/* ── Test 3: Event signaling ───────────────────────────────── */

static HANDLE go_event;

static DWORD WINAPI event_thread(LPVOID param) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written;
    (void)param;

    /* Wait for signal */
    WaitForSingleObject(go_event, INFINITE);

    const char *msg = "[Event] Received signal!\n";
    WriteFile(hOut, msg, 25, &written, NULL);
    return 0;
}

/* ── Test 4: Interlocked ───────────────────────────────────── */

static volatile LONG interlocked_val = 0;

static DWORD WINAPI interlocked_thread(LPVOID param) {
    (void)param;
    for (int i = 0; i < 50; i++) {
        InterlockedIncrement(&interlocked_val);
    }
    return 0;
}

/* ── Main ──────────────────────────────────────────────────── */

static void print(const char *s) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written;
    int len = 0;
    while (s[len]) len++;
    WriteFile(hOut, s, (DWORD)len, &written, NULL);
}

void mainCRTStartup(void) {
    print("=== Win32 Threading Test ===\n\n");

    /* Test 1: Spawn 3 threads, wait for them */
    print("-- Test 1: CreateThread + WaitForSingleObject --\n");
    HANDLE threads[3];
    for (int i = 0; i < 3; i++) {
        threads[i] = CreateThread(NULL, 0, thread_func, (LPVOID)(DWORD)i, 0, NULL);
    }
    for (int i = 0; i < 3; i++) {
        WaitForSingleObject(threads[i], INFINITE);
        DWORD exitCode = 0;
        GetExitCodeThread(threads[i], &exitCode);
        CloseHandle(threads[i]);
    }
    print("Test 1: PASS\n\n");

    /* Test 2: Critical section with 2 threads */
    print("-- Test 2: Critical Section --\n");
    InitializeCriticalSection(&cs);
    shared_counter = 0;
    HANDLE t1 = CreateThread(NULL, 0, cs_thread, NULL, 0, NULL);
    HANDLE t2 = CreateThread(NULL, 0, cs_thread, NULL, 0, NULL);
    WaitForSingleObject(t1, INFINITE);
    WaitForSingleObject(t2, INFINITE);
    CloseHandle(t1);
    CloseHandle(t2);
    DeleteCriticalSection(&cs);
    /* 2 threads x 100 increments = 200 */
    if (shared_counter == 200)
        print("Test 2: PASS (counter=200)\n\n");
    else
        print("Test 2: FAIL\n\n");

    /* Test 3: Event signaling */
    print("-- Test 3: Events --\n");
    go_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    HANDLE et = CreateThread(NULL, 0, event_thread, NULL, 0, NULL);
    Sleep(50);  /* let thread start and block on event */
    SetEvent(go_event);
    WaitForSingleObject(et, INFINITE);
    CloseHandle(et);
    CloseHandle(go_event);
    print("Test 3: PASS\n\n");

    /* Test 4: Interlocked operations */
    print("-- Test 4: Interlocked --\n");
    interlocked_val = 0;
    HANDLE it1 = CreateThread(NULL, 0, interlocked_thread, NULL, 0, NULL);
    HANDLE it2 = CreateThread(NULL, 0, interlocked_thread, NULL, 0, NULL);
    WaitForSingleObject(it1, INFINITE);
    WaitForSingleObject(it2, INFINITE);
    CloseHandle(it1);
    CloseHandle(it2);
    /* 2 threads x 50 increments = 100 */
    if (interlocked_val == 100)
        print("Test 4: PASS (val=100)\n\n");
    else
        print("Test 4: FAIL\n\n");

    print("=== All threading tests complete ===\n");
    ExitProcess(0);
}
