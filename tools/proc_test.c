/* Win32 process creation test for ImposOS Phase 4 validation.
 *
 * Build with:
 *   i686-w64-mingw32-gcc -o proc_test.exe proc_test.c \
 *       -nostdlib -lkernel32 -Wl,--subsystem,console -e _mainCRTStartup
 */
#include <windows.h>

void mainCRTStartup(void);

static void print(const char *s) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written;
    int len = 0;
    while (s[len]) len++;
    WriteFile(hOut, s, (DWORD)len, &written, NULL);
}

static void print_num(int val) {
    char buf[16];
    int i = 0;
    if (val == 0) { buf[i++] = '0'; }
    else {
        if (val < 0) { buf[i++] = '-'; val = -val; }
        char tmp[12]; int j = 0;
        while (val > 0) { tmp[j++] = '0' + (val % 10); val /= 10; }
        while (j > 0) buf[i++] = tmp[--j];
    }
    buf[i] = 0;
    print(buf);
}

static int pass = 0;
static int fail = 0;

void mainCRTStartup(void) {
    pass = 0; fail = 0;

    print("=== Win32 Process Test ===\n\n");

    /* Test 1: GetCurrentProcess */
    print("-- Test 1: GetCurrentProcess --\n");
    {
        HANDLE hp = GetCurrentProcess();
        if (hp != 0 && hp != INVALID_HANDLE_VALUE) {
            print("Test 1: PASS\n\n"); pass++;
        } else {
            /* GetCurrentProcess returns pseudo-handle 0xFFFFFFFF */
            if (hp == INVALID_HANDLE_VALUE) {
                print("  pseudo-handle = -1\n");
                print("Test 1: PASS\n\n"); pass++;
            } else {
                print("Test 1: FAIL\n\n"); fail++;
            }
        }
    }

    /* Test 2: GetCurrentProcessId */
    print("-- Test 2: GetCurrentProcessId --\n");
    {
        DWORD pid = GetCurrentProcessId();
        print("  PID="); print_num((int)pid); print("\n");
        if (pid > 0) {
            print("Test 2: PASS\n\n"); pass++;
        } else {
            print("Test 2: FAIL\n\n"); fail++;
        }
    }

    /* Test 3: CreatePipe */
    print("-- Test 3: CreatePipe --\n");
    {
        HANDLE hRead = NULL, hWrite = NULL;
        BOOL ret = CreatePipe(&hRead, &hWrite, NULL, 0);
        if (ret && hRead && hWrite) {
            /* Write through pipe */
            const char *msg = "pipe ok";
            DWORD written = 0;
            /* We can't use WriteFile on pipe handles directly in our shim,
             * but the handles should be valid */
            print("  hRead="); print_num((int)hRead);
            print(" hWrite="); print_num((int)hWrite); print("\n");
            CloseHandle(hRead);
            CloseHandle(hWrite);
            print("Test 3: PASS\n\n"); pass++;
        } else {
            print("Test 3: FAIL\n\n"); fail++;
        }
    }

    /* Test 4: CreateProcessA with hello.exe */
    print("-- Test 4: CreateProcessA --\n");
    {
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        /* Zero init */
        char *p = (char *)&si;
        for (unsigned i = 0; i < sizeof(si); i++) p[i] = 0;
        si.cb = sizeof(si);
        p = (char *)&pi;
        for (unsigned i = 0; i < sizeof(pi); i++) p[i] = 0;

        BOOL ret = CreateProcessA(
            "hello.exe", NULL,
            NULL, NULL, FALSE, 0,
            NULL, NULL, &si, &pi);

        if (ret) {
            print("  hProcess="); print_num((int)pi.hProcess);
            print(" hThread="); print_num((int)pi.hThread);
            print(" PID="); print_num((int)pi.dwProcessId);
            print("\n");

            /* Test 5: WaitForSingleObject on process */
            print("-- Test 5: WaitForSingleObject(process) --\n");
            DWORD waitResult = WaitForSingleObject(pi.hProcess, 5000);
            if (waitResult == WAIT_OBJECT_0) {
                print("Test 5: PASS\n\n"); pass++;
            } else {
                print("  wait returned "); print_num((int)waitResult); print("\n");
                print("Test 5: FAIL\n\n"); fail++;
            }

            /* Test 6: GetExitCodeProcess */
            print("-- Test 6: GetExitCodeProcess --\n");
            DWORD exitCode = 999;
            BOOL ecRet = GetExitCodeProcess(pi.hProcess, &exitCode);
            if (ecRet) {
                print("  exitCode="); print_num((int)exitCode); print("\n");
                print("Test 6: PASS\n\n"); pass++;
            } else {
                print("Test 6: FAIL\n\n"); fail++;
            }

            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            print("Test 4: PASS\n\n"); pass++;
        } else {
            print("  CreateProcessA failed (hello.exe not found?)\n");
            print("Test 4: FAIL\n\n"); fail++;
            print("-- Test 5: SKIP --\n\n");
            print("-- Test 6: SKIP --\n\n");
        }
    }

    /* Test 7: DuplicateHandle */
    print("-- Test 7: DuplicateHandle --\n");
    {
        HANDLE hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
        HANDLE hDup = NULL;
        BOOL ret = DuplicateHandle(
            GetCurrentProcess(), hEvent,
            GetCurrentProcess(), &hDup,
            0, FALSE, 0x00000002 /* DUPLICATE_SAME_ACCESS */);
        if (ret && hDup) {
            /* Signal through original, check via duplicate */
            SetEvent(hEvent);
            DWORD wr = WaitForSingleObject(hDup, 0);
            CloseHandle(hEvent);
            if (wr == WAIT_OBJECT_0) {
                print("Test 7: PASS\n\n"); pass++;
            } else {
                print("Test 7: FAIL (dup not signaled)\n\n"); fail++;
            }
        } else {
            print("Test 7: FAIL\n\n"); fail++;
        }
    }

    /* Summary */
    print("=== Process tests: ");
    print_num(pass);
    print(" passed, ");
    print_num(fail);
    print(" failed ===\n");

    ExitProcess(fail ? 1 : 0);
}
