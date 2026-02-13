/* Win32 memory management test for ImposOS Phase 2 validation.
 *
 * Build with:
 *   i686-w64-mingw32-gcc -o mem_test.exe mem_test.c \
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

static void print_hex(DWORD val) {
    char buf[16] = "0x";
    const char *hex = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--)
        buf[2 + (7 - i)] = hex[(val >> (i * 4)) & 0xF];
    buf[10] = 0;
    print(buf);
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

void mainCRTStartup(void) {
    int pass = 0, fail = 0;

    print("=== Win32 Memory Test ===\n\n");

    /* Test 1: VirtualAlloc + read/write */
    print("-- Test 1: VirtualAlloc PAGE_READWRITE --\n");
    BYTE *p = (BYTE *)VirtualAlloc(NULL, 8192, MEM_COMMIT, PAGE_READWRITE);
    if (p) {
        /* Write pattern */
        for (int i = 0; i < 8192; i++)
            p[i] = (BYTE)(i & 0xFF);
        /* Verify */
        int ok = 1;
        for (int i = 0; i < 8192; i++) {
            if (p[i] != (BYTE)(i & 0xFF)) { ok = 0; break; }
        }
        if (ok) { print("Test 1: PASS\n\n"); pass++; }
        else    { print("Test 1: FAIL (data mismatch)\n\n"); fail++; }
    } else {
        print("Test 1: FAIL (alloc returned NULL)\n\n"); fail++;
    }

    /* Test 2: VirtualAlloc returns zeroed memory */
    print("-- Test 2: VirtualAlloc zeroed --\n");
    BYTE *z = (BYTE *)VirtualAlloc(NULL, 4096, MEM_COMMIT, PAGE_READWRITE);
    if (z) {
        int ok = 1;
        for (int i = 0; i < 4096; i++) {
            if (z[i] != 0) { ok = 0; break; }
        }
        if (ok) { print("Test 2: PASS\n\n"); pass++; }
        else    { print("Test 2: FAIL (not zeroed)\n\n"); fail++; }
        VirtualFree(z, 0, MEM_RELEASE);
    } else {
        print("Test 2: FAIL (alloc returned NULL)\n\n"); fail++;
    }

    /* Test 3: VirtualProtect */
    print("-- Test 3: VirtualProtect --\n");
    if (p) {
        DWORD old_prot = 0;
        BOOL ret = VirtualProtect(p, 4096, PAGE_READONLY, &old_prot);
        if (ret && old_prot == PAGE_READWRITE) {
            print("  Changed to PAGE_READONLY, old=PAGE_READWRITE\n");
            /* Change back to writable */
            VirtualProtect(p, 4096, PAGE_READWRITE, &old_prot);
            print("Test 3: PASS\n\n"); pass++;
        } else {
            print("Test 3: FAIL (protect change failed)\n\n"); fail++;
        }
    } else {
        print("Test 3: SKIP (no alloc)\n\n");
    }

    /* Test 4: VirtualQuery */
    print("-- Test 4: VirtualQuery --\n");
    if (p) {
        MEMORY_BASIC_INFORMATION mbi;
        DWORD ret = VirtualQuery(p, &mbi, sizeof(mbi));
        if (ret >= sizeof(mbi)) {
            print("  Base="); print_hex((DWORD)mbi.BaseAddress);
            print(" Size="); print_num((int)mbi.RegionSize);
            print(" State="); print_hex(mbi.State);
            print("\n");
            if (mbi.RegionSize >= 8192 && mbi.State == MEM_COMMIT) {
                print("Test 4: PASS\n\n"); pass++;
            } else {
                print("Test 4: FAIL (bad info)\n\n"); fail++;
            }
        } else {
            print("Test 4: FAIL (query returned 0)\n\n"); fail++;
        }
    } else {
        print("Test 4: SKIP (no alloc)\n\n");
    }

    /* Test 5: VirtualFree */
    print("-- Test 5: VirtualFree --\n");
    if (p) {
        BOOL ret = VirtualFree(p, 0, MEM_RELEASE);
        if (ret) { print("Test 5: PASS\n\n"); pass++; }
        else     { print("Test 5: FAIL\n\n"); fail++; }
    } else {
        print("Test 5: SKIP\n\n");
    }

    /* Test 6: GlobalAlloc / GlobalLock / GlobalFree */
    print("-- Test 6: GlobalAlloc --\n");
    HGLOBAL hg = GlobalAlloc(GPTR, 256);
    if (hg) {
        BYTE *gp = (BYTE *)GlobalLock(hg);
        if (gp) {
            /* GPTR = zeroinit, check */
            int ok = 1;
            for (int i = 0; i < 256; i++) {
                if (gp[i] != 0) { ok = 0; break; }
            }
            /* Write and read back */
            gp[0] = 0xAB;
            gp[255] = 0xCD;
            if (gp[0] != 0xAB || gp[255] != 0xCD) ok = 0;
            GlobalUnlock(hg);
            GlobalFree(hg);
            if (ok) { print("Test 6: PASS\n\n"); pass++; }
            else    { print("Test 6: FAIL (data error)\n\n"); fail++; }
        } else {
            print("Test 6: FAIL (lock returned NULL)\n\n"); fail++;
        }
    } else {
        print("Test 6: FAIL (alloc returned NULL)\n\n"); fail++;
    }

    /* Test 7: Multiple VirtualAlloc — verify distinct addresses */
    print("-- Test 7: Multiple allocations --\n");
    BYTE *a1 = (BYTE *)VirtualAlloc(NULL, 4096, MEM_COMMIT, PAGE_READWRITE);
    BYTE *a2 = (BYTE *)VirtualAlloc(NULL, 4096, MEM_COMMIT, PAGE_READWRITE);
    BYTE *a3 = (BYTE *)VirtualAlloc(NULL, 4096, MEM_COMMIT, PAGE_READWRITE);
    if (a1 && a2 && a3 && a1 != a2 && a2 != a3 && a1 != a3) {
        a1[0] = 1; a2[0] = 2; a3[0] = 3;
        if (a1[0] == 1 && a2[0] == 2 && a3[0] == 3) {
            print("Test 7: PASS\n\n"); pass++;
        } else {
            print("Test 7: FAIL (data overlap)\n\n"); fail++;
        }
        VirtualFree(a1, 0, MEM_RELEASE);
        VirtualFree(a2, 0, MEM_RELEASE);
        VirtualFree(a3, 0, MEM_RELEASE);
    } else {
        print("Test 7: FAIL (alloc failed or overlapping)\n\n"); fail++;
    }

    /* Summary */
    print("=== Memory tests: ");
    print_num(pass);
    print(" passed, ");
    print_num(fail);
    print(" failed ===\n");

    ExitProcess(fail ? 1 : 0);
}
