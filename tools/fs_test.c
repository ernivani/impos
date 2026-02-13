/* Win32 file system & I/O test for ImposOS Phase 3 validation.
 *
 * Build with:
 *   i686-w64-mingw32-gcc -o fs_test.exe fs_test.c \
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

    print("=== Win32 File System Test ===\n\n");

    /* Test 1: CreateFileA + WriteFile + CloseHandle */
    print("-- Test 1: CreateFile + Write --\n");
    HANDLE hf = CreateFileA("_fstest.txt", GENERIC_WRITE, 0, NULL,
                            CREATE_ALWAYS, 0, NULL);
    if (hf != INVALID_HANDLE_VALUE) {
        const char *data = "Hello FS test!";
        DWORD written = 0;
        WriteFile(hf, data, 14, &written, NULL);
        CloseHandle(hf);
        if (written == 14) { print("Test 1: PASS\n\n"); pass++; }
        else { print("Test 1: FAIL (written mismatch)\n\n"); fail++; }
    } else {
        print("Test 1: FAIL (create failed)\n\n"); fail++;
    }

    /* Test 2: ReadFile + verify contents */
    print("-- Test 2: ReadFile --\n");
    hf = CreateFileA("_fstest.txt", GENERIC_READ, 0, NULL,
                     OPEN_EXISTING, 0, NULL);
    if (hf != INVALID_HANDLE_VALUE) {
        char buf[64] = {0};
        DWORD bytesRead = 0;
        ReadFile(hf, buf, 64, &bytesRead, NULL);
        CloseHandle(hf);
        int ok = (bytesRead == 14);
        for (int i = 0; i < 14 && ok; i++) {
            if (buf[i] != "Hello FS test!"[i]) ok = 0;
        }
        if (ok) { print("Test 2: PASS\n\n"); pass++; }
        else { print("Test 2: FAIL (content mismatch)\n\n"); fail++; }
    } else {
        print("Test 2: FAIL (open failed)\n\n"); fail++;
    }

    /* Test 3: SetFilePointer */
    print("-- Test 3: SetFilePointer --\n");
    hf = CreateFileA("_fstest.txt", GENERIC_READ, 0, NULL,
                     OPEN_EXISTING, 0, NULL);
    if (hf != INVALID_HANDLE_VALUE) {
        SetFilePointer(hf, 6, NULL, FILE_BEGIN);
        char buf[16] = {0};
        DWORD bytesRead = 0;
        ReadFile(hf, buf, 8, &bytesRead, NULL);
        CloseHandle(hf);
        /* Should read "FS test!" (8 bytes starting at offset 6) */
        int ok = (bytesRead == 8);
        const char *expect = "FS test!";
        for (int i = 0; i < 8 && ok; i++) {
            if (buf[i] != expect[i]) ok = 0;
        }
        if (ok) { print("Test 3: PASS\n\n"); pass++; }
        else { print("Test 3: FAIL\n\n"); fail++; }
    } else {
        print("Test 3: FAIL (open failed)\n\n"); fail++;
    }

    /* Test 4: GetFileAttributesA */
    print("-- Test 4: GetFileAttributes --\n");
    {
        DWORD attr = GetFileAttributesA("_fstest.txt");
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            print("Test 4: PASS\n\n"); pass++;
        } else {
            print("Test 4: FAIL\n\n"); fail++;
        }
    }

    /* Test 5: GetFileSize */
    print("-- Test 5: GetFileSize --\n");
    hf = CreateFileA("_fstest.txt", GENERIC_READ, 0, NULL,
                     OPEN_EXISTING, 0, NULL);
    if (hf != INVALID_HANDLE_VALUE) {
        DWORD sz = GetFileSize(hf, NULL);
        CloseHandle(hf);
        if (sz == 14) { print("Test 5: PASS\n\n"); pass++; }
        else {
            print("Test 5: FAIL (size="); print_num((int)sz);
            print(")\n\n"); fail++;
        }
    } else {
        print("Test 5: FAIL (open failed)\n\n"); fail++;
    }

    /* Test 6: CreateDirectoryA + GetFileAttributes on dir */
    print("-- Test 6: CreateDirectory --\n");
    {
        BOOL ret = CreateDirectoryA("_testdir", NULL);
        DWORD attr = GetFileAttributesA("_testdir");
        if (ret && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
            print("Test 6: PASS\n\n"); pass++;
        } else {
            print("Test 6: FAIL\n\n"); fail++;
        }
    }

    /* Test 7: FindFirstFileA / FindNextFileA */
    print("-- Test 7: FindFirstFile/FindNextFile --\n");
    {
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA("*", &fd);
        int count = 0;
        if (hFind != INVALID_HANDLE_VALUE) {
            count = 1;
            while (FindNextFileA(hFind, &fd))
                count++;
            FindClose(hFind);
        }
        if (count >= 2) {
            /* Should find at least _fstest.txt and _testdir */
            print("  Found "); print_num(count); print(" entries\n");
            print("Test 7: PASS\n\n"); pass++;
        } else {
            print("Test 7: FAIL (count="); print_num(count);
            print(")\n\n"); fail++;
        }
    }

    /* Test 8: CopyFileA */
    print("-- Test 8: CopyFile --\n");
    {
        BOOL ret = CopyFileA("_fstest.txt", "_fscopy.txt", FALSE);
        if (ret) {
            /* Verify copy contents */
            HANDLE hc = CreateFileA("_fscopy.txt", GENERIC_READ, 0, NULL,
                                    OPEN_EXISTING, 0, NULL);
            if (hc != INVALID_HANDLE_VALUE) {
                char buf[64] = {0};
                DWORD bytesRead = 0;
                ReadFile(hc, buf, 64, &bytesRead, NULL);
                CloseHandle(hc);
                if (bytesRead == 14) { print("Test 8: PASS\n\n"); pass++; }
                else { print("Test 8: FAIL (copy size wrong)\n\n"); fail++; }
            } else {
                print("Test 8: FAIL (can't open copy)\n\n"); fail++;
            }
        } else {
            print("Test 8: FAIL (copy failed)\n\n"); fail++;
        }
    }

    /* Test 9: DeleteFileA */
    print("-- Test 9: DeleteFile --\n");
    {
        BOOL ret = DeleteFileA("_fscopy.txt");
        DWORD attr = GetFileAttributesA("_fscopy.txt");
        if (ret && attr == INVALID_FILE_ATTRIBUTES) {
            print("Test 9: PASS\n\n"); pass++;
        } else {
            print("Test 9: FAIL\n\n"); fail++;
        }
    }

    /* Test 10: GetCurrentDirectoryA + GetFullPathNameA */
    print("-- Test 10: GetCurrentDirectory + GetFullPathName --\n");
    {
        char cwd[MAX_PATH] = {0};
        DWORD len = GetCurrentDirectoryA(MAX_PATH, cwd);
        char full[MAX_PATH] = {0};
        GetFullPathNameA("_fstest.txt", MAX_PATH, full, NULL);
        if (len > 0 && full[0] == '/') {
            print("  cwd="); print(cwd);
            print("  full="); print(full); print("\n");
            print("Test 10: PASS\n\n"); pass++;
        } else {
            print("Test 10: FAIL\n\n"); fail++;
        }
    }

    /* Cleanup */
    DeleteFileA("_fstest.txt");
    RemoveDirectoryA("_testdir");

    /* Summary */
    print("=== FS tests: ");
    print_num(pass);
    print(" passed, ");
    print_num(fail);
    print(" failed ===\n");

    ExitProcess(fail ? 1 : 0);
}
