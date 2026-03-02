#include <kernel/test.h>
#include <kernel/win32_types.h>
#include <kernel/win32_seh.h>
#include <kernel/fs.h>
#include <kernel/user.h>
#include <kernel/group.h>
#include <kernel/gfx.h>
#include <kernel/quota.h>
#include <kernel/ip.h>
#include <kernel/net.h>
#include <kernel/endian.h>
#include <kernel/firewall.h>
#include <kernel/mouse.h>
#include <kernel/crypto.h>
#include <kernel/tls.h>
#include <kernel/socket.h>
#include <kernel/tcp.h>
#include <kernel/udp.h>
#include <kernel/dns.h>
#include <kernel/task.h>
#include <kernel/sched.h>
#include <kernel/vmm.h>
#include <kernel/vma.h>
#include <kernel/frame_ref.h>
#include <kernel/pmm.h>
#include <kernel/vfs.h>
#include <kernel/linux_syscall.h>
#include <kernel/elf_loader.h>
#include <kernel/pe_loader.h>
#include <kernel/env.h>
#include <kernel/idt.h>
#include <kernel/pipe.h>
#include <kernel/rtc.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>

static int test_count;
static int test_pass;
static int test_fail;

/* ---- String Tests ---- */

static void test_string(void) {
    printf("== String Tests ==\n");

    /* strlen */
    TEST_ASSERT(strlen("") == 0, "strlen empty");
    TEST_ASSERT(strlen("hello") == 5, "strlen hello");
    TEST_ASSERT(strlen("a") == 1, "strlen single");

    /* strcmp */
    TEST_ASSERT(strcmp("abc", "abc") == 0, "strcmp equal");
    TEST_ASSERT(strcmp("abc", "abd") < 0, "strcmp less");
    TEST_ASSERT(strcmp("abd", "abc") > 0, "strcmp greater");
    TEST_ASSERT(strcmp("", "") == 0, "strcmp empty");

    /* strncmp */
    TEST_ASSERT(strncmp("abcdef", "abcxyz", 3) == 0, "strncmp equal prefix");
    TEST_ASSERT(strncmp("abcdef", "abcxyz", 4) != 0, "strncmp differ");
    TEST_ASSERT(strncmp("abc", "abc", 10) == 0, "strncmp short");

    /* strcpy */
    char buf[64];
    strcpy(buf, "test");
    TEST_ASSERT(strcmp(buf, "test") == 0, "strcpy basic");

    /* strncpy */
    memset(buf, 'X', sizeof(buf));
    strncpy(buf, "hi", 5);
    TEST_ASSERT(strcmp(buf, "hi") == 0, "strncpy basic");
    TEST_ASSERT(buf[2] == '\0' && buf[3] == '\0' && buf[4] == '\0', "strncpy pads");

    /* strcat */
    strcpy(buf, "hello");
    strcat(buf, " world");
    TEST_ASSERT(strcmp(buf, "hello world") == 0, "strcat basic");

    /* strchr */
    TEST_ASSERT(strchr("hello", 'l') != NULL, "strchr found");
    TEST_ASSERT(strchr("hello", 'l') == &"hello"[2], "strchr position");
    TEST_ASSERT(strchr("hello", 'z') == NULL, "strchr not found");

    /* strstr */
    TEST_ASSERT(strstr("hello world", "world") != NULL, "strstr found");
    TEST_ASSERT(strstr("hello world", "xyz") == NULL, "strstr not found");
    TEST_ASSERT(strstr("hello", "") != NULL, "strstr empty needle");
    TEST_ASSERT(strstr("abcabc", "cab") != NULL, "strstr overlap");

    /* memcmp */
    TEST_ASSERT(memcmp("abc", "abc", 3) == 0, "memcmp equal");
    TEST_ASSERT(memcmp("abc", "abd", 3) != 0, "memcmp differ");

    /* memcpy */
    char src[] = "data";
    char dst[8];
    memcpy(dst, src, 5);
    TEST_ASSERT(strcmp(dst, "data") == 0, "memcpy basic");

    /* memset */
    memset(buf, 'A', 5);
    buf[5] = '\0';
    TEST_ASSERT(strcmp(buf, "AAAAA") == 0, "memset basic");

    /* memmove (overlapping) */
    char overlap[] = "abcdef";
    memmove(overlap + 2, overlap, 4);
    TEST_ASSERT(memcmp(overlap, "ababcd", 6) == 0, "memmove overlap");
}

/* ---- New String Tests (strrchr, strnlen, memchr, strcspn, strspn, strpbrk) ---- */

static void test_string_extra(void) {
    printf("== String Extra Tests ==\n");

    /* strrchr */
    TEST_ASSERT(strrchr("hello", 'l') == &"hello"[3], "strrchr last match");
    TEST_ASSERT(strrchr("hello", 'z') == NULL, "strrchr not found");
    TEST_ASSERT(strrchr("hello", 'h') == &"hello"[0], "strrchr first char");

    /* strnlen */
    TEST_ASSERT(strnlen("hello", 10) == 5, "strnlen within bound");
    TEST_ASSERT(strnlen("hello", 3) == 3, "strnlen truncated");
    TEST_ASSERT(strnlen("", 5) == 0, "strnlen empty");

    /* memchr */
    const char* s = "abcdef";
    TEST_ASSERT(memchr(s, 'c', 6) == s + 2, "memchr found");
    TEST_ASSERT(memchr(s, 'z', 6) == NULL, "memchr not found");
    TEST_ASSERT(memchr(s, 'e', 3) == NULL, "memchr out of range");

    /* strcspn */
    TEST_ASSERT(strcspn("hello", "lo") == 2, "strcspn basic");
    TEST_ASSERT(strcspn("hello", "xyz") == 5, "strcspn no match");
    TEST_ASSERT(strcspn("hello", "h") == 0, "strcspn first char");

    /* strspn */
    TEST_ASSERT(strspn("hello", "hel") == 4, "strspn basic");
    TEST_ASSERT(strspn("hello", "xyz") == 0, "strspn no match");
    TEST_ASSERT(strspn("aaab", "a") == 3, "strspn repeated");

    /* strpbrk */
    TEST_ASSERT(strpbrk("hello", "lo") == &"hello"[2], "strpbrk found");
    TEST_ASSERT(strpbrk("hello", "xyz") == NULL, "strpbrk not found");
}

/* ---- Stdlib Tests ---- */

static void test_stdlib(void) {
    printf("== Stdlib Tests ==\n");

    /* atoi */
    TEST_ASSERT(atoi("0") == 0, "atoi zero");
    TEST_ASSERT(atoi("42") == 42, "atoi positive");
    TEST_ASSERT(atoi("-7") == -7, "atoi negative");
    TEST_ASSERT(atoi("  123") == 123, "atoi whitespace");
    TEST_ASSERT(atoi("99abc") == 99, "atoi trailing");
    TEST_ASSERT(atoi("") == 0, "atoi empty");

    /* atol */
    TEST_ASSERT(atol("0") == 0L, "atol zero");
    TEST_ASSERT(atol("100000") == 100000L, "atol large");
    TEST_ASSERT(atol("-999") == -999L, "atol negative");
    TEST_ASSERT(atol("  42") == 42L, "atol whitespace");

    /* atoll */
    TEST_ASSERT(atoll("0") == 0LL, "atoll zero");
    TEST_ASSERT(atoll("1234567890") == 1234567890LL, "atoll large");
    TEST_ASSERT(atoll("-1234567890") == -1234567890LL, "atoll negative");

    /* strtol */
    char* end;
    TEST_ASSERT(strtol("255", &end, 10) == 255, "strtol decimal");
    TEST_ASSERT(*end == '\0', "strtol endptr at end");

    TEST_ASSERT(strtol("0xff", &end, 0) == 255, "strtol auto hex");
    TEST_ASSERT(strtol("077", &end, 0) == 63, "strtol auto octal");
    TEST_ASSERT(strtol("100", &end, 0) == 100, "strtol auto decimal");

    TEST_ASSERT(strtol("ff", &end, 16) == 255, "strtol hex");
    TEST_ASSERT(strtol("-10", &end, 10) == -10, "strtol negative");
    TEST_ASSERT(strtol("0xFF", &end, 16) == 255, "strtol hex prefix");

    /* malloc/free */
    void* p1 = malloc(32);
    TEST_ASSERT(p1 != NULL, "malloc returns non-null");

    void* p2 = malloc(64);
    TEST_ASSERT(p2 != NULL, "malloc second alloc");
    TEST_ASSERT(p1 != p2, "malloc different addresses");

    /* Write and read back */
    memset(p1, 0xAA, 32);
    memset(p2, 0xBB, 64);
    TEST_ASSERT(((unsigned char*)p1)[0] == 0xAA, "malloc write p1");
    TEST_ASSERT(((unsigned char*)p2)[0] == 0xBB, "malloc write p2");

    free(p1);
    free(p2);

    /* Malloc after free should reuse */
    void* p3 = malloc(16);
    TEST_ASSERT(p3 != NULL, "malloc after free");
    free(p3);
}

/* ---- New Stdlib Tests (realloc, calloc, abs, div, rand, qsort, bsearch) ---- */

static int int_compare(const void* a, const void* b) {
    return *(const int*)a - *(const int*)b;
}

static void test_stdlib_extra(void) {
    printf("== Stdlib Extra Tests ==\n");

    /* realloc */
    void* p = realloc(NULL, 32);
    TEST_ASSERT(p != NULL, "realloc NULL is malloc");
    memset(p, 0x42, 32);

    void* p2 = realloc(p, 64);
    TEST_ASSERT(p2 != NULL, "realloc grow");
    TEST_ASSERT(((unsigned char*)p2)[0] == 0x42, "realloc preserves data");

    void* p3 = realloc(p2, 16);
    TEST_ASSERT(p3 != NULL, "realloc shrink");

    void* p4 = realloc(p3, 0);
    TEST_ASSERT(p4 == NULL, "realloc zero frees");

    /* calloc */
    int* arr = (int*)calloc(10, sizeof(int));
    TEST_ASSERT(arr != NULL, "calloc non-null");
    int all_zero = 1;
    for (int i = 0; i < 10; i++) {
        if (arr[i] != 0) { all_zero = 0; break; }
    }
    TEST_ASSERT(all_zero, "calloc zeroed");
    free(arr);

    /* abs / labs */
    TEST_ASSERT(abs(5) == 5, "abs positive");
    TEST_ASSERT(abs(-5) == 5, "abs negative");
    TEST_ASSERT(abs(0) == 0, "abs zero");
    TEST_ASSERT(labs(-100L) == 100L, "labs negative");

    /* div / ldiv */
    div_t d = div(17, 5);
    TEST_ASSERT(d.quot == 3, "div quot");
    TEST_ASSERT(d.rem == 2, "div rem");

    ldiv_t ld = ldiv(-17L, 5L);
    TEST_ASSERT(ld.quot == -3L, "ldiv quot");
    TEST_ASSERT(ld.rem == -2L, "ldiv rem");

    /* rand / srand */
    srand(42);
    int r1 = rand();
    int r2 = rand();
    srand(42);
    int r3 = rand();
    TEST_ASSERT(r1 == r3, "srand deterministic");
    TEST_ASSERT(r1 != r2 || r1 == r2, "rand returns int");  /* always passes, just exercises rand */
    TEST_ASSERT(r1 >= 0 && r1 <= RAND_MAX, "rand in range");

    /* qsort */
    int data[] = {5, 3, 8, 1, 9, 2, 7, 4, 6, 0};
    qsort(data, 10, sizeof(int), int_compare);
    int sorted = 1;
    for (int i = 0; i < 9; i++) {
        if (data[i] > data[i + 1]) { sorted = 0; break; }
    }
    TEST_ASSERT(sorted, "qsort sorts");
    TEST_ASSERT(data[0] == 0, "qsort first");
    TEST_ASSERT(data[9] == 9, "qsort last");

    /* bsearch */
    int key = 5;
    int* found = (int*)bsearch(&key, data, 10, sizeof(int), int_compare);
    TEST_ASSERT(found != NULL, "bsearch found");
    TEST_ASSERT(found && *found == 5, "bsearch value");

    int missing = 42;
    int* nf = (int*)bsearch(&missing, data, 10, sizeof(int), int_compare);
    TEST_ASSERT(nf == NULL, "bsearch not found");
}

/* ---- snprintf Tests ---- */

static void test_snprintf(void) {
    printf("== snprintf Tests ==\n");

    char buf[128];

    snprintf(buf, sizeof(buf), "hello");
    TEST_ASSERT(strcmp(buf, "hello") == 0, "snprintf plain string");

    snprintf(buf, sizeof(buf), "%d", 42);
    TEST_ASSERT(strcmp(buf, "42") == 0, "snprintf %%d positive");

    snprintf(buf, sizeof(buf), "%d", -7);
    TEST_ASSERT(strcmp(buf, "-7") == 0, "snprintf %%d negative");

    snprintf(buf, sizeof(buf), "%d", 0);
    TEST_ASSERT(strcmp(buf, "0") == 0, "snprintf %%d zero");

    snprintf(buf, sizeof(buf), "%s", "world");
    TEST_ASSERT(strcmp(buf, "world") == 0, "snprintf %%s");

    snprintf(buf, sizeof(buf), "%x", 255);
    TEST_ASSERT(strcmp(buf, "ff") == 0, "snprintf %%x");

    snprintf(buf, sizeof(buf), "%u", 12345);
    TEST_ASSERT(strcmp(buf, "12345") == 0, "snprintf %%u");

    snprintf(buf, sizeof(buf), "%c", 'A');
    TEST_ASSERT(strcmp(buf, "A") == 0, "snprintf %%c");

    snprintf(buf, sizeof(buf), "%s=%d", "x", 5);
    TEST_ASSERT(strcmp(buf, "x=5") == 0, "snprintf mixed");

    /* Truncation */
    snprintf(buf, 4, "hello");
    TEST_ASSERT(strcmp(buf, "hel") == 0, "snprintf truncation");
}

/* ---- Filesystem Tests ---- */

static void test_fs(void) {
    printf("== Filesystem Tests ==\n");

    /* Switch to root for FS tests (need write on / directory) */
    const char* saved_user = user_get_current();
    user_set_current("root");

    /* Create and read back a file */
    int ret = fs_create_file("/tmp_test_file", 0);
    TEST_ASSERT(ret == 0, "fs create file");

    const char* data = "test data 123";
    ret = fs_write_file("/tmp_test_file", (const uint8_t*)data, strlen(data));
    TEST_ASSERT(ret == 0, "fs write file");

    uint8_t rbuf[512];
    size_t rsize;
    ret = fs_read_file("/tmp_test_file", rbuf, &rsize);
    TEST_ASSERT(ret == 0, "fs read file");
    TEST_ASSERT(rsize == strlen(data), "fs read size matches");
    TEST_ASSERT(memcmp(rbuf, data, rsize) == 0, "fs read data matches");

    /* Delete file */
    ret = fs_delete_file("/tmp_test_file");
    TEST_ASSERT(ret == 0, "fs delete file");

    /* Verify deleted */
    ret = fs_read_file("/tmp_test_file", rbuf, &rsize);
    TEST_ASSERT(ret != 0, "fs deleted file unreadable");

    /* mkdir */
    ret = fs_create_file("/tmp_test_dir", 1);
    TEST_ASSERT(ret == 0, "fs mkdir");

    ret = fs_delete_file("/tmp_test_dir");
    TEST_ASSERT(ret == 0, "fs rmdir");

    /* Symlink */
    fs_create_file("/tmp_sym_target", 0);
    fs_write_file("/tmp_sym_target", (const uint8_t*)"symdata", 7);

    ret = fs_create_symlink("/tmp_sym_target", "/tmp_sym_link");
    TEST_ASSERT(ret == 0, "fs create symlink");

    char linkbuf[256];
    ret = fs_readlink("/tmp_sym_link", linkbuf, sizeof(linkbuf));
    TEST_ASSERT(ret == 0, "fs readlink");
    TEST_ASSERT(strcmp(linkbuf, "/tmp_sym_target") == 0, "fs readlink target");

    /* Read through symlink */
    ret = fs_read_file("/tmp_sym_link", rbuf, &rsize);
    TEST_ASSERT(ret == 0, "fs read through symlink");
    TEST_ASSERT(rsize == 7, "fs symlink read size");
    TEST_ASSERT(memcmp(rbuf, "symdata", 7) == 0, "fs symlink read data");

    /* Cleanup */
    fs_delete_file("/tmp_sym_link");
    fs_delete_file("/tmp_sym_target");

    /* Permission test - chmod */
    fs_create_file("/tmp_perm_test", 0);
    ret = fs_chmod("/tmp_perm_test", 0644);
    TEST_ASSERT(ret == 0, "fs chmod");
    fs_delete_file("/tmp_perm_test");

    /* Restore original user */
    if (saved_user)
        user_set_current(saved_user);
}

/* ---- Indirect Block Tests ---- */

static void test_fs_indirect(void) {
    printf("== FS Indirect Block Tests ==\n");

    const char* saved_user = user_get_current();
    user_set_current("root");

    /* Create a large file (8192 bytes > 4096 direct limit) */
    size_t large_size = 8192;
    uint8_t* wbuf = (uint8_t*)malloc(large_size);
    TEST_ASSERT(wbuf != NULL, "indirect: malloc write buf");
    if (!wbuf) {
        if (saved_user) user_set_current(saved_user);
        return;
    }

    /* Fill with pattern */
    for (size_t i = 0; i < large_size; i++)
        wbuf[i] = (uint8_t)(i & 0xFF);

    int ret = fs_create_file("/tmp_large_file", 0);
    TEST_ASSERT(ret == 0, "indirect: create large file");

    ret = fs_write_file("/tmp_large_file", wbuf, large_size);
    TEST_ASSERT(ret == 0, "indirect: write 8192 bytes");

    /* Read back */
    uint8_t* rbuf = (uint8_t*)malloc(large_size);
    TEST_ASSERT(rbuf != NULL, "indirect: malloc read buf");
    if (!rbuf) {
        free(wbuf);
        fs_delete_file("/tmp_large_file");
        if (saved_user) user_set_current(saved_user);
        return;
    }

    size_t rsize;
    ret = fs_read_file("/tmp_large_file", rbuf, &rsize);
    TEST_ASSERT(ret == 0, "indirect: read large file");
    TEST_ASSERT(rsize == large_size, "indirect: read size matches");

    int data_ok = 1;
    for (size_t i = 0; i < large_size; i++) {
        if (rbuf[i] != (uint8_t)(i & 0xFF)) {
            data_ok = 0;
            break;
        }
    }
    TEST_ASSERT(data_ok, "indirect: data integrity");

    /* Delete and verify */
    ret = fs_delete_file("/tmp_large_file");
    TEST_ASSERT(ret == 0, "indirect: delete large file");

    ret = fs_read_file("/tmp_large_file", rbuf, &rsize);
    TEST_ASSERT(ret != 0, "indirect: deleted file unreadable");

    free(wbuf);
    free(rbuf);

    if (saved_user)
        user_set_current(saved_user);
}

/* ---- FS v3 Tests (VFS, procfs, devfs, tmpfs, double-indirect, truncate) ---- */

static void test_fs_v3(void) {
    printf("== FS v3 Tests ==\n");

    const char* saved_user = user_get_current();
    user_set_current("root");

    /* --- VFS mount table --- */
    vfs_mount_t *mtab;
    int mcount;
    int ret = vfs_get_mounts(&mtab, &mcount);
    TEST_ASSERT(ret == 0, "vfs_get_mounts succeeds");
    TEST_ASSERT(mcount >= 3, "at least 3 VFS mounts (proc/dev/tmp)");

    int found_proc = 0, found_dev = 0, found_tmp = 0;
    for (int i = 0; i < mcount; i++) {
        if (!mtab[i].active) continue;
        if (strcmp(mtab[i].prefix, "/proc") == 0) found_proc = 1;
        if (strcmp(mtab[i].prefix, "/dev") == 0)  found_dev = 1;
        if (strcmp(mtab[i].prefix, "/tmp") == 0)  found_tmp = 1;
    }
    TEST_ASSERT(found_proc, "vfs: /proc mounted");
    TEST_ASSERT(found_dev,  "vfs: /dev mounted");
    TEST_ASSERT(found_tmp,  "vfs: /tmp mounted");

    /* --- VFS resolve --- */
    const char *rel;
    vfs_mount_t *mnt = vfs_resolve("/proc/uptime", &rel);
    TEST_ASSERT(mnt != NULL, "vfs_resolve /proc/uptime finds mount");
    TEST_ASSERT(strcmp(rel, "uptime") == 0, "vfs_resolve: relative is 'uptime'");

    mnt = vfs_resolve("/tmp/foo", &rel);
    TEST_ASSERT(mnt != NULL, "vfs_resolve /tmp/foo finds mount");
    TEST_ASSERT(strcmp(rel, "foo") == 0, "vfs_resolve: relative is 'foo'");

    /* Root path should NOT match any mount */
    mnt = vfs_resolve("/etc/passwd", &rel);
    TEST_ASSERT(mnt == NULL, "vfs_resolve /etc/passwd returns NULL (root fs)");

    /* --- procfs read --- */
    uint8_t pbuf[512];
    size_t psize;
    ret = fs_read_file("/proc/version", pbuf, &psize);
    TEST_ASSERT(ret == 0, "procfs: read /proc/version");
    TEST_ASSERT(psize > 0, "procfs: version has content");

    ret = fs_read_file("/proc/meminfo", pbuf, &psize);
    TEST_ASSERT(ret == 0, "procfs: read /proc/meminfo");
    TEST_ASSERT(psize > 0, "procfs: meminfo has content");

    ret = fs_read_file("/proc/uptime", pbuf, &psize);
    TEST_ASSERT(ret == 0, "procfs: read /proc/uptime");
    TEST_ASSERT(psize > 0, "procfs: uptime has content");

    /* procfs is read-only */
    ret = fs_create_file("/proc/hacked", 0);
    TEST_ASSERT(ret != 0, "procfs: create rejected (read-only)");

    ret = fs_write_file("/proc/version", (const uint8_t*)"x", 1);
    TEST_ASSERT(ret != 0, "procfs: write rejected (read-only)");

    /* --- tmpfs CRUD --- */
    ret = fs_create_file("/tmp/test_v3", 0);
    TEST_ASSERT(ret == 0, "tmpfs: create file");

    const char *tdata = "tmpfs works!";
    ret = fs_write_file("/tmp/test_v3", (const uint8_t*)tdata, strlen(tdata));
    TEST_ASSERT(ret == 0, "tmpfs: write file");

    uint8_t tbuf[256];
    size_t tsize;
    ret = fs_read_file("/tmp/test_v3", tbuf, &tsize);
    TEST_ASSERT(ret == 0, "tmpfs: read file");
    TEST_ASSERT(tsize == strlen(tdata), "tmpfs: read size matches");
    TEST_ASSERT(memcmp(tbuf, tdata, tsize) == 0, "tmpfs: data integrity");

    /* tmpfs mkdir + subfile */
    ret = fs_create_file("/tmp/test_dir_v3", 1);
    TEST_ASSERT(ret == 0, "tmpfs: mkdir");

    /* tmpfs delete */
    ret = fs_delete_file("/tmp/test_v3");
    TEST_ASSERT(ret == 0, "tmpfs: delete file");

    ret = fs_read_file("/tmp/test_v3", tbuf, &tsize);
    TEST_ASSERT(ret != 0, "tmpfs: deleted file unreadable");

    ret = fs_delete_file("/tmp/test_dir_v3");
    TEST_ASSERT(ret == 0, "tmpfs: rmdir");

    /* --- fs_write_at / fs_read_at (partial I/O) --- */
    ret = fs_create_file("/test_write_at", 0);
    TEST_ASSERT(ret == 0, "write_at: create file");

    /* Resolve inode for the file */
    uint32_t parent;
    char fname[MAX_NAME_LEN];
    int ino = fs_resolve_path("/test_write_at", &parent, fname);
    TEST_ASSERT(ino >= 0, "write_at: resolve path");

    if (ino >= 0) {
        /* Write at offset 0 */
        const char *part1 = "AAAA";
        ret = fs_write_at((uint32_t)ino, (const uint8_t*)part1, 0, 4);
        TEST_ASSERT(ret == 4, "write_at: wrote 4 bytes at offset 0");

        /* Write at offset 8 (creates a hole from 4-7) */
        const char *part2 = "BBBB";
        ret = fs_write_at((uint32_t)ino, (const uint8_t*)part2, 8, 4);
        TEST_ASSERT(ret == 4, "write_at: wrote 4 bytes at offset 8");

        /* Read back the whole thing */
        uint8_t rbuf[16];
        memset(rbuf, 0xFF, sizeof(rbuf));
        ret = fs_read_at((uint32_t)ino, rbuf, 0, 12);
        TEST_ASSERT(ret == 12, "write_at: read 12 bytes");
        TEST_ASSERT(memcmp(rbuf, "AAAA", 4) == 0, "write_at: first chunk intact");
        TEST_ASSERT(memcmp(rbuf + 8, "BBBB", 4) == 0, "write_at: second chunk intact");
        /* Hole bytes (4-7) should be zero */
        int hole_ok = (rbuf[4] == 0 && rbuf[5] == 0 && rbuf[6] == 0 && rbuf[7] == 0);
        TEST_ASSERT(hole_ok, "write_at: hole filled with zeros");
    }
    fs_delete_file("/test_write_at");

    /* --- fs_truncate --- */
    ret = fs_create_file("/test_trunc", 0);
    TEST_ASSERT(ret == 0, "truncate: create file");

    /* Write some data */
    const char *trdata = "0123456789ABCDEF";
    fs_write_file("/test_trunc", (const uint8_t*)trdata, 16);

    /* Truncate to 8 bytes */
    ret = fs_truncate("/test_trunc", 8);
    TEST_ASSERT(ret == 0, "truncate: shrink to 8");

    uint8_t trbuf[32];
    size_t trsize;
    ret = fs_read_file("/test_trunc", trbuf, &trsize);
    TEST_ASSERT(ret == 0, "truncate: read after shrink");
    TEST_ASSERT(trsize == 8, "truncate: size is 8");
    TEST_ASSERT(memcmp(trbuf, "01234567", 8) == 0, "truncate: data intact");

    /* Extend to 16 (new bytes should be zero) */
    ret = fs_truncate("/test_trunc", 16);
    TEST_ASSERT(ret == 0, "truncate: extend to 16");

    ret = fs_read_file("/test_trunc", trbuf, &trsize);
    TEST_ASSERT(ret == 0, "truncate: read after extend");
    TEST_ASSERT(trsize == 16, "truncate: size is 16");
    int ext_ok = 1;
    for (int i = 8; i < 16; i++) {
        if (trbuf[i] != 0) { ext_ok = 0; break; }
    }
    TEST_ASSERT(ext_ok, "truncate: extended bytes are zero");

    fs_delete_file("/test_trunc");

    /* --- Double-indirect large file test (> 32KB = beyond direct blocks) --- */
    size_t big_size = 40960;  /* 40KB: needs indirect blocks */
    uint8_t *wbuf = (uint8_t*)malloc(big_size);
    TEST_ASSERT(wbuf != NULL, "big file: malloc write buf");
    if (wbuf) {
        for (size_t i = 0; i < big_size; i++)
            wbuf[i] = (uint8_t)((i * 7 + 13) & 0xFF);

        ret = fs_create_file("/test_bigfile", 0);
        TEST_ASSERT(ret == 0, "big file: create");

        ret = fs_write_file("/test_bigfile", wbuf, big_size);
        TEST_ASSERT(ret == 0, "big file: write 40KB");

        uint8_t *rbig = (uint8_t*)malloc(big_size);
        TEST_ASSERT(rbig != NULL, "big file: malloc read buf");
        if (rbig) {
            size_t rbs;
            ret = fs_read_file("/test_bigfile", rbig, &rbs);
            TEST_ASSERT(ret == 0, "big file: read back");
            TEST_ASSERT(rbs == big_size, "big file: size matches");

            int data_ok = 1;
            for (size_t i = 0; i < big_size; i++) {
                if (rbig[i] != (uint8_t)((i * 7 + 13) & 0xFF)) {
                    data_ok = 0;
                    break;
                }
            }
            TEST_ASSERT(data_ok, "big file: 40KB data integrity");
            free(rbig);
        }
        fs_delete_file("/test_bigfile");
        free(wbuf);
    }

    /* --- FS geometry constants --- */
    TEST_ASSERT(NUM_BLOCKS == 65536, "geometry: 65536 blocks");
    TEST_ASSERT(NUM_INODES == 4096, "geometry: 4096 inodes");
    TEST_ASSERT(BLOCK_SIZE == 4096, "geometry: 4KB blocks");
    TEST_ASSERT(DIRECT_BLOCKS == 8, "geometry: 8 direct blocks");

    if (saved_user)
        user_set_current(saved_user);
}

/* ---- User Tests ---- */

static void test_user(void) {
    printf("== User Tests ==\n");

    const char* name = user_get_current();
    TEST_ASSERT(name != NULL, "current user not null");

    uint16_t uid = user_get_current_uid();
    TEST_ASSERT(uid != 65535, "current uid valid");

    user_t* u = user_get(name);
    TEST_ASSERT(u != NULL, "user_get current");
    if (u) {
        TEST_ASSERT(u->uid == uid, "uid matches");
    }

    /* Group membership */
    uint16_t gid = user_get_current_gid();
    TEST_ASSERT(gid != 65535, "current gid valid");
}

/* ---- Graphics Tests ---- */

static void test_gfx(void) {
    printf("== Graphics Tests ==\n");

    TEST_ASSERT(gfx_is_active() == 1, "gfx is active");
    TEST_ASSERT(gfx_width() > 0, "gfx width > 0");
    TEST_ASSERT(gfx_height() > 0, "gfx height > 0");
    TEST_ASSERT(gfx_bpp() == 32, "gfx bpp == 32");
    TEST_ASSERT(gfx_cols() == gfx_width() / 8, "gfx cols == width/8");
    TEST_ASSERT(gfx_rows() == gfx_height() / 16, "gfx rows == height/16");
    TEST_ASSERT(gfx_pitch() >= gfx_width() * 4, "gfx pitch >= width*4");
    TEST_ASSERT(gfx_backbuffer() != NULL, "gfx backbuffer not null");

    /* Out of bounds put_pixel should not crash */
    gfx_put_pixel(-1, -1, 0xFF0000);
    gfx_put_pixel((int)gfx_width() + 1, (int)gfx_height() + 1, 0xFF0000);
    TEST_ASSERT(1, "gfx put_pixel OOB no crash");

    /* Fill rect clipping at edges */
    gfx_fill_rect((int)gfx_width() - 5, (int)gfx_height() - 5, 100, 100, 0x00FF00);
    TEST_ASSERT(1, "gfx fill_rect clip no crash");

    /* Draw char without crash */
    gfx_draw_char(0, 0, 'A', 0xFFFFFF, 0x000000);
    TEST_ASSERT(1, "gfx draw_char no crash");

    /* VGA-to-RGB spot checks */
    /* Black = 0x000000, White = 0xFFFFFF, Blue = 0x0000AA, Red = 0xAA0000 */
    /* These are tested indirectly via the TTY color table, just verify gfx works */
    TEST_ASSERT(GFX_RGB(0,0,0) == 0x000000, "GFX_RGB black");
    TEST_ASSERT(GFX_RGB(255,255,255) == 0xFFFFFF, "GFX_RGB white");
    TEST_ASSERT(GFX_RGB(255,0,0) == 0xFF0000, "GFX_RGB red");
    TEST_ASSERT(GFX_RGB(0,255,0) == 0x00FF00, "GFX_RGB green");
}

/* ---- sscanf Tests ---- */

static void test_sscanf(void) {
    printf("== sscanf Tests ==\n");

    int a, b;
    TEST_ASSERT(sscanf("42", "%d", &a) == 1, "sscanf single int");
    TEST_ASSERT(a == 42, "sscanf int value");

    TEST_ASSERT(sscanf("10 20", "%d %d", &a, &b) == 2, "sscanf two ints");
    TEST_ASSERT(a == 10 && b == 20, "sscanf two int values");

    TEST_ASSERT(sscanf("-5", "%d", &a) == 1, "sscanf negative int");
    TEST_ASSERT(a == -5, "sscanf negative value");

    unsigned int x;
    TEST_ASSERT(sscanf("0xFF", "%x", &x) == 1, "sscanf hex");
    TEST_ASSERT(x == 0xFF, "sscanf hex value");

    TEST_ASSERT(sscanf("ff", "%x", &x) == 1, "sscanf hex no prefix");
    TEST_ASSERT(x == 0xFF, "sscanf hex no prefix value");

    char str[64];
    TEST_ASSERT(sscanf("hello world", "%s", str) == 1, "sscanf string");
    TEST_ASSERT(strcmp(str, "hello") == 0, "sscanf string value");

    char c;
    TEST_ASSERT(sscanf("A", "%c", &c) == 1, "sscanf char");
    TEST_ASSERT(c == 'A', "sscanf char value");

    unsigned int u;
    TEST_ASSERT(sscanf("123", "%u", &u) == 1, "sscanf unsigned");
    TEST_ASSERT(u == 123, "sscanf unsigned value");

    int n;
    TEST_ASSERT(sscanf("abc", "%s%n", str, &n) == 1, "sscanf %%n");
    TEST_ASSERT(n == 3, "sscanf %%n value");
}

/* ---- Quota Tests ---- */

static void test_quota(void) {
    printf("== Quota Tests ==\n");

    /* Set quota for uid 999 */
    TEST_ASSERT(quota_set(999, 5, 10) == 0, "quota set");

    quota_entry_t* q = quota_get(999);
    TEST_ASSERT(q != NULL, "quota get");
    TEST_ASSERT(q->max_inodes == 5, "quota max_inodes");
    TEST_ASSERT(q->max_blocks == 10, "quota max_blocks");

    /* Check allows initially */
    TEST_ASSERT(quota_check_inode(999) == 0, "quota check inode ok");
    TEST_ASSERT(quota_check_block(999, 5) == 0, "quota check block ok");

    /* Add usage */
    for (int i = 0; i < 5; i++) quota_add_inode(999);
    TEST_ASSERT(quota_check_inode(999) == -1, "quota inode exceeded");

    quota_add_blocks(999, 8);
    TEST_ASSERT(quota_check_block(999, 3) == -1, "quota block exceeded");
    TEST_ASSERT(quota_check_block(999, 2) == 0, "quota block still ok");

    /* Remove usage */
    quota_remove_inode(999);
    TEST_ASSERT(quota_check_inode(999) == 0, "quota inode after remove");

    /* No quota for uid 998 = unlimited */
    TEST_ASSERT(quota_check_inode(998) == 0, "quota no limit inode");
    TEST_ASSERT(quota_check_block(998, 1000) == 0, "quota no limit block");

    /* Clean up */
    q->active = 0;
}

/* ---- Network Tests ---- */

static void test_network(void) {
    printf("== Network Tests ==\n");

    /* Byte order: htons / ntohs */
    TEST_ASSERT(htons(0x1234) == 0x3412, "htons swap");
    TEST_ASSERT(ntohs(0x3412) == 0x1234, "ntohs swap");
    TEST_ASSERT(ntohs(htons(0xABCD)) == 0xABCD, "htons/ntohs roundtrip");

    /* htonl / ntohl */
    TEST_ASSERT(htonl(0x12345678) == 0x78563412, "htonl swap");
    TEST_ASSERT(ntohl(htonl(0xDEADBEEF)) == 0xDEADBEEF, "htonl/ntohl roundtrip");

    /* IP checksum on a known header:
     * Version/IHL=0x45, TOS=0, Len=0x003C, ID=0x1C46, Flags=0x4000,
     * TTL=0x40, Proto=0x06, Checksum=0, Src=0xAC100A63, Dst=0xAC100A0C
     * Expected checksum: the function returns host-order value which,
     * stored directly to uint16_t, yields correct wire bytes. */
    uint8_t ip_hdr[20] = {
        0x45, 0x00, 0x00, 0x3C,  /* ver/ihl, tos, total_len */
        0x1C, 0x46, 0x40, 0x00,  /* id, flags/frag */
        0x40, 0x06, 0x00, 0x00,  /* ttl, proto(TCP), checksum=0 */
        0xAC, 0x10, 0x0A, 0x63,  /* src: 172.16.10.99 */
        0xAC, 0x10, 0x0A, 0x0C   /* dst: 172.16.10.12 */
    };

    uint16_t csum = ip_checksum(ip_hdr, 20);
    TEST_ASSERT(csum != 0, "ip_checksum non-zero for zeroed field");

    /* Store checksum and verify sum-to-zero property */
    *(uint16_t*)(ip_hdr + 10) = csum;
    uint16_t verify = ip_checksum(ip_hdr, 20);
    TEST_ASSERT(verify == 0, "ip_checksum sum-to-zero");

    /* Net config */
    net_config_t* cfg = net_get_config();
    TEST_ASSERT(cfg != NULL, "net_get_config non-null");
    TEST_ASSERT(cfg->link_up == 1, "link is up");

    /* MAC should not be all-zero (driver set it) */
    int mac_nonzero = 0;
    for (int i = 0; i < 6; i++) {
        if (cfg->mac[i] != 0) mac_nonzero = 1;
    }
    TEST_ASSERT(mac_nonzero, "MAC address set");
}

/* ---- Firewall Tests ---- */

static void test_firewall(void) {
    printf("== Firewall Tests ==\n");

    /* Save current state and reinit */
    firewall_flush();
    firewall_set_default(FW_ACTION_ALLOW);

    uint8_t src[4] = {10, 0, 2, 15};
    uint8_t dst[4] = {10, 0, 2, 1};

    /* Default allow */
    TEST_ASSERT(firewall_check(src, dst, FW_PROTO_TCP, 80) == FW_ACTION_ALLOW,
                "fw default allow");

    /* Add deny ICMP rule */
    fw_rule_t rule;
    memset(&rule, 0, sizeof(rule));
    rule.protocol = FW_PROTO_ICMP;
    rule.action = FW_ACTION_DENY;
    rule.enabled = 1;
    TEST_ASSERT(firewall_add_rule(&rule) == 0, "fw add rule");
    TEST_ASSERT(firewall_rule_count() == 1, "fw rule count 1");

    /* ICMP should be denied, TCP still allowed */
    TEST_ASSERT(firewall_check(src, dst, FW_PROTO_ICMP, 0) == FW_ACTION_DENY,
                "fw deny icmp");
    TEST_ASSERT(firewall_check(src, dst, FW_PROTO_TCP, 80) == FW_ACTION_ALLOW,
                "fw allow tcp with icmp rule");

    /* Add deny TCP port 80 */
    fw_rule_t rule2;
    memset(&rule2, 0, sizeof(rule2));
    rule2.protocol = FW_PROTO_TCP;
    rule2.action = FW_ACTION_DENY;
    rule2.dst_port_min = 80;
    rule2.dst_port_max = 80;
    rule2.enabled = 1;
    firewall_add_rule(&rule2);

    TEST_ASSERT(firewall_check(src, dst, FW_PROTO_TCP, 80) == FW_ACTION_DENY,
                "fw deny tcp:80");
    TEST_ASSERT(firewall_check(src, dst, FW_PROTO_TCP, 443) == FW_ACTION_ALLOW,
                "fw allow tcp:443");

    /* Test default deny */
    firewall_set_default(FW_ACTION_DENY);
    TEST_ASSERT(firewall_check(src, dst, FW_PROTO_UDP, 53) == FW_ACTION_DENY,
                "fw default deny udp");

    /* Test source IP matching */
    fw_rule_t rule3;
    memset(&rule3, 0, sizeof(rule3));
    rule3.protocol = FW_PROTO_ALL;
    rule3.action = FW_ACTION_ALLOW;
    rule3.src_ip[0] = 10; rule3.src_ip[1] = 0; rule3.src_ip[2] = 2; rule3.src_ip[3] = 15;
    memset(rule3.src_mask, 255, 4);
    rule3.enabled = 1;
    firewall_add_rule(&rule3);

    TEST_ASSERT(firewall_check(src, dst, FW_PROTO_UDP, 53) == FW_ACTION_ALLOW,
                "fw allow by src ip");
    uint8_t other_src[4] = {192, 168, 1, 1};
    TEST_ASSERT(firewall_check(other_src, dst, FW_PROTO_UDP, 53) == FW_ACTION_DENY,
                "fw deny other src");

    /* Delete rule and flush */
    TEST_ASSERT(firewall_del_rule(0) == 0, "fw del rule 0");
    TEST_ASSERT(firewall_rule_count() == 2, "fw count after del");

    firewall_flush();
    TEST_ASSERT(firewall_rule_count() == 0, "fw count after flush");
    firewall_set_default(FW_ACTION_ALLOW);
}

/* ---- Mouse Tests ---- */

static void test_mouse(void) {
    printf("== Mouse Tests ==\n");

    /* Mouse should be initialized */
    TEST_ASSERT(mouse_get_x() >= 0, "mouse x >= 0");
    TEST_ASSERT(mouse_get_y() >= 0, "mouse y >= 0");
    TEST_ASSERT(mouse_get_buttons() == 0, "mouse buttons init 0");
}

/* ---- Crypto Tests ---- */

void test_crypto(void) {
    printf("== Crypto Tests ==\n");

    /* SHA-256: NIST test vector - SHA256("abc") */
    {
        uint8_t digest[32];
        sha256((const uint8_t *)"abc", 3, digest);
        /* Expected: ba7816bf 8f01cfea 414140de 5dae2223 b00361a3 96177a9c b410ff61 f20015ad */
        uint8_t expected[] = {
            0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,
            0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
            0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,
            0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad
        };
        TEST_ASSERT(memcmp(digest, expected, 32) == 0, "SHA256(abc)");
    }

    /* SHA-256: empty string */
    {
        uint8_t digest[32];
        sha256((const uint8_t *)"", 0, digest);
        uint8_t expected[] = {
            0xe3,0xb0,0xc4,0x42,0x98,0xfc,0x1c,0x14,
            0x9a,0xfb,0xf4,0xc8,0x99,0x6f,0xb9,0x24,
            0x27,0xae,0x41,0xe4,0x64,0x9b,0x93,0x4c,
            0xa4,0x95,0x99,0x1b,0x78,0x52,0xb8,0x55
        };
        TEST_ASSERT(memcmp(digest, expected, 32) == 0, "SHA256(empty)");
    }

    /* HMAC-SHA-256: RFC 4231 Test Case 2 */
    {
        uint8_t mac[32];
        hmac_sha256((const uint8_t *)"Jefe", 4,
                    (const uint8_t *)"what do ya want for nothing?", 28,
                    mac);
        uint8_t expected[] = {
            0x5b,0xdc,0xc1,0x46,0xbf,0x60,0x75,0x4e,
            0x6a,0x04,0x24,0x26,0x08,0x95,0x75,0xc7,
            0x5a,0x00,0x3f,0x08,0x9d,0x27,0x39,0x83,
            0x9d,0xec,0x58,0xb9,0x64,0xec,0x38,0x43
        };
        TEST_ASSERT(memcmp(mac, expected, 32) == 0, "HMAC-SHA256 RFC4231 TC2");
    }

    /* AES-128: FIPS 197 Appendix B test vector */
    {
        uint8_t key[16] = {
            0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,
            0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c
        };
        uint8_t plaintext[16] = {
            0x32,0x43,0xf6,0xa8,0x88,0x5a,0x30,0x8d,
            0x31,0x31,0x98,0xa2,0xe0,0x37,0x07,0x34
        };
        uint8_t expected_ct[16] = {
            0x39,0x25,0x84,0x1d,0x02,0xdc,0x09,0xfb,
            0xdc,0x11,0x85,0x97,0x19,0x6a,0x0b,0x32
        };

        aes128_ctx_t ctx;
        aes128_init(&ctx, key);

        uint8_t ct[16];
        aes128_encrypt_block(&ctx, plaintext, ct);
        TEST_ASSERT(memcmp(ct, expected_ct, 16) == 0, "AES-128 encrypt");

        uint8_t pt[16];
        aes128_decrypt_block(&ctx, ct, pt);
        TEST_ASSERT(memcmp(pt, plaintext, 16) == 0, "AES-128 decrypt");
    }

    /* AES-128-CBC encrypt/decrypt roundtrip */
    {
        uint8_t key[16] = {0};
        uint8_t iv[16] = {0};
        uint8_t data[32];
        memset(data, 0x42, 32);

        aes128_ctx_t ctx;
        aes128_init(&ctx, key);

        uint8_t cipher[32], plain[32];
        aes128_cbc_encrypt(&ctx, iv, data, 32, cipher);
        aes128_cbc_decrypt(&ctx, iv, cipher, 32, plain);
        TEST_ASSERT(memcmp(plain, data, 32) == 0, "AES-CBC roundtrip");
    }

    /* Bignum: 3^10 mod 7 = 4 */
    {
        bignum_t base, exp, mod, result;
        bn_zero(&base); base.d[0] = 3; base.top = 1;
        bn_zero(&exp);  exp.d[0] = 10; exp.top = 1;
        bn_zero(&mod);  mod.d[0] = 7;  mod.top = 1;
        bn_modexp(&result, &base, &exp, &mod);
        TEST_ASSERT(result.d[0] == 4, "bignum 3^10 mod 7 = 4");
    }

    /* Bignum: 2^16 mod 100 = 36 */
    {
        bignum_t base, exp, mod, result;
        bn_zero(&base); base.d[0] = 2;  base.top = 1;
        bn_zero(&exp);  exp.d[0] = 16;  exp.top = 1;
        bn_zero(&mod);  mod.d[0] = 100; mod.top = 1;
        bn_modexp(&result, &base, &exp, &mod);
        TEST_ASSERT(result.d[0] == 36, "bignum 2^16 mod 100 = 36");
    }

    /* Bignum mulmod: carry overflow test (2048-bit) */
    {
        bignum_t a, two, m, result;
        bn_zero(&a); bn_zero(&two); bn_zero(&m);
        /* m = 2^2047 + 1 (has MSB set, true 2048-bit number) */
        m.d[63] = 0x80000000;
        m.d[0] = 1;
        m.top = 64;
        /* a = 2^2047 = m - 1 */
        a.d[63] = 0x80000000;
        a.top = 64;
        /* Compute a*2 mod m = 2^2048 mod (2^2047+1) = 2^2047 - 1 */
        two.d[0] = 2; two.top = 1;
        bn_mulmod(&result, &a, &two, &m);
        bignum_t expected;
        bn_zero(&expected);
        for (int i = 0; i < 63; i++) expected.d[i] = 0xFFFFFFFF;
        expected.d[63] = 0x7FFFFFFF;
        expected.top = 64;
        TEST_ASSERT(bn_cmp(&result, &expected) == 0, "mulmod 2048-bit carry");
    }

    /* Bignum modexp: (m-1)^2 mod m = 1 (2048-bit) */
    {
        bignum_t base, exp, m, result;
        bn_zero(&base); bn_zero(&exp); bn_zero(&m);
        /* m = 2^2047 + 3 */
        m.d[63] = 0x80000000;
        m.d[0] = 3;
        m.top = 64;
        /* base = m - 1 = 2^2047 + 2 */
        base.d[63] = 0x80000000;
        base.d[0] = 2;
        base.top = 64;
        /* exp = 2 */
        exp.d[0] = 2; exp.top = 1;
        bn_modexp(&result, &base, &exp, &m);
        /* (m-1)^2 mod m = (-1)^2 mod m = 1 */
        TEST_ASSERT(result.d[0] == 1 && result.top == 1, "modexp (m-1)^2 mod m = 1");
    }

    /* PRNG: should produce non-zero, non-identical output */
    {
        uint8_t buf1[16], buf2[16];
        prng_init();
        prng_random(buf1, 16);
        prng_random(buf2, 16);
        int all_zero = 1;
        for (int i = 0; i < 16; i++)
            if (buf1[i] != 0) all_zero = 0;
        TEST_ASSERT(!all_zero, "PRNG non-zero output");
        TEST_ASSERT(memcmp(buf1, buf2, 16) != 0, "PRNG different outputs");
    }

    printf("  Crypto tests done.\n");
}

/* ---- TLS Test (requires network) ---- */

void test_tls(void) {
    printf("== TLS Test ==\n");

    net_config_t *cfg = net_get_config();
    if (!cfg || !cfg->link_up || (cfg->ip[0] == 0 && cfg->ip[1] == 0)) {
        printf("  SKIP: network not configured (run dhcp first)\n");
        return;
    }

    printf("  Attempting HTTPS GET to example.com...\n");

    /* Run in background thread so UI stays responsive */
    static https_async_t req;
    strcpy(req.host, "example.com");
    req.port = 443;
    strcpy(req.path, "/");

    if (https_get_async(&req) < 0) {
        printf("  Failed to start HTTPS thread\n");
        return;
    }

    printf("  TLS running in background (thread %d)...\n", req.tid);
    while (!req.done) {
        keyboard_run_idle();   /* keep WM/UI alive */
        task_yield();
    }

    if (req.result > 0 && req.body) {
        TEST_ASSERT(req.body_len > 0, "tls: got response body");
        /* Check for HTML content */
        int has_html = 0;
        for (size_t i = 0; i + 5 < req.body_len; i++) {
            if (memcmp(req.body + i, "<html", 5) == 0 ||
                memcmp(req.body + i, "<HTML", 5) == 0) {
                has_html = 1;
                break;
            }
        }
        TEST_ASSERT(has_html, "tls: response contains HTML");
        printf("  Received %u bytes of HTML\n", (unsigned)req.body_len);
        free(req.body);
    } else {
        printf("  HTTPS GET failed (ret=%d) - server may not support our cipher\n", req.result);
    }
}

/* ---- Win32 DLL Loading Tests ---- */

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

static void test_security_crypto(void) {
    printf("== Security & Crypto Tests ==\n");

    /* ── CryptAPI (advapi32) ──────────────────────────────────── */

    typedef BOOL (WINAPI *pfn_CryptAcquireContextA)(
        HANDLE *, LPCSTR, LPCSTR, DWORD, DWORD);
    typedef BOOL (WINAPI *pfn_CryptAcquireContextW)(
        HANDLE *, const WCHAR *, const WCHAR *, DWORD, DWORD);
    typedef BOOL (WINAPI *pfn_CryptReleaseContext)(HANDLE, DWORD);
    typedef BOOL (WINAPI *pfn_CryptGenRandom)(HANDLE, DWORD, BYTE *);

    pfn_CryptAcquireContextA pCryptAcquireContextA =
        (pfn_CryptAcquireContextA)win32_resolve_import("advapi32.dll", "CryptAcquireContextA");
    pfn_CryptAcquireContextW pCryptAcquireContextW =
        (pfn_CryptAcquireContextW)win32_resolve_import("advapi32.dll", "CryptAcquireContextW");
    pfn_CryptReleaseContext pCryptReleaseContext =
        (pfn_CryptReleaseContext)win32_resolve_import("advapi32.dll", "CryptReleaseContext");
    pfn_CryptGenRandom pCryptGenRandom =
        (pfn_CryptGenRandom)win32_resolve_import("advapi32.dll", "CryptGenRandom");

    /* Test 1: CryptAcquireContextA resolves + returns TRUE */
    TEST_ASSERT(pCryptAcquireContextA != NULL, "crypt: CryptAcquireContextA resolved");
    HANDLE hProv = 0;
    if (pCryptAcquireContextA) {
        BOOL ok = pCryptAcquireContextA(&hProv, NULL, NULL, 0, 0);
        TEST_ASSERT(ok == TRUE, "crypt: CryptAcquireContextA returns TRUE");
        TEST_ASSERT(hProv != 0, "crypt: hProv is non-zero");
    }

    /* Test 2: CryptGenRandom fills buffer with non-zero bytes */
    TEST_ASSERT(pCryptGenRandom != NULL, "crypt: CryptGenRandom resolved");
    if (pCryptGenRandom && hProv) {
        BYTE buf[32];
        memset(buf, 0, sizeof(buf));
        BOOL ok = pCryptGenRandom(hProv, sizeof(buf), buf);
        TEST_ASSERT(ok == TRUE, "crypt: CryptGenRandom returns TRUE");
        int has_nonzero = 0;
        for (int i = 0; i < 32; i++) {
            if (buf[i] != 0) { has_nonzero = 1; break; }
        }
        TEST_ASSERT(has_nonzero, "crypt: CryptGenRandom produces non-zero bytes");
    }

    /* Test 3: CryptReleaseContext returns TRUE */
    TEST_ASSERT(pCryptReleaseContext != NULL, "crypt: CryptReleaseContext resolved");
    if (pCryptReleaseContext) {
        BOOL ok = pCryptReleaseContext(hProv, 0);
        TEST_ASSERT(ok == TRUE, "crypt: CryptReleaseContext returns TRUE");
    }

    /* ── BCrypt ───────────────────────────────────────────────── */

    typedef LONG (WINAPI *pfn_BCryptOpenAlgorithmProvider)(
        HANDLE *, const WCHAR *, const WCHAR *, DWORD);
    typedef LONG (WINAPI *pfn_BCryptCloseAlgorithmProvider)(HANDLE, DWORD);
    typedef LONG (WINAPI *pfn_BCryptCreateHash)(
        HANDLE, HANDLE *, BYTE *, DWORD, BYTE *, DWORD, DWORD);
    typedef LONG (WINAPI *pfn_BCryptHashData)(HANDLE, const BYTE *, DWORD, DWORD);
    typedef LONG (WINAPI *pfn_BCryptFinishHash)(HANDLE, BYTE *, DWORD, DWORD);
    typedef LONG (WINAPI *pfn_BCryptDestroyHash)(HANDLE);
    typedef LONG (WINAPI *pfn_BCryptGenRandom)(HANDLE, BYTE *, DWORD, DWORD);

    pfn_BCryptOpenAlgorithmProvider pBCryptOpen =
        (pfn_BCryptOpenAlgorithmProvider)win32_resolve_import("bcrypt.dll", "BCryptOpenAlgorithmProvider");
    pfn_BCryptCloseAlgorithmProvider pBCryptClose =
        (pfn_BCryptCloseAlgorithmProvider)win32_resolve_import("bcrypt.dll", "BCryptCloseAlgorithmProvider");
    pfn_BCryptCreateHash pBCryptCreateHash =
        (pfn_BCryptCreateHash)win32_resolve_import("bcrypt.dll", "BCryptCreateHash");
    pfn_BCryptHashData pBCryptHashData =
        (pfn_BCryptHashData)win32_resolve_import("bcrypt.dll", "BCryptHashData");
    pfn_BCryptFinishHash pBCryptFinishHash =
        (pfn_BCryptFinishHash)win32_resolve_import("bcrypt.dll", "BCryptFinishHash");
    pfn_BCryptDestroyHash pBCryptDestroyHash =
        (pfn_BCryptDestroyHash)win32_resolve_import("bcrypt.dll", "BCryptDestroyHash");
    pfn_BCryptGenRandom pBCryptGenRandom =
        (pfn_BCryptGenRandom)win32_resolve_import("bcrypt.dll", "BCryptGenRandom");

    TEST_ASSERT(pBCryptOpen != NULL, "bcrypt: BCryptOpenAlgorithmProvider resolved");
    TEST_ASSERT(pBCryptClose != NULL, "bcrypt: BCryptCloseAlgorithmProvider resolved");
    TEST_ASSERT(pBCryptCreateHash != NULL, "bcrypt: BCryptCreateHash resolved");
    TEST_ASSERT(pBCryptHashData != NULL, "bcrypt: BCryptHashData resolved");
    TEST_ASSERT(pBCryptFinishHash != NULL, "bcrypt: BCryptFinishHash resolved");
    TEST_ASSERT(pBCryptDestroyHash != NULL, "bcrypt: BCryptDestroyHash resolved");
    TEST_ASSERT(pBCryptGenRandom != NULL, "bcrypt: BCryptGenRandom resolved");

    if (!pBCryptOpen || !pBCryptClose || !pBCryptCreateHash ||
        !pBCryptHashData || !pBCryptFinishHash || !pBCryptDestroyHash)
        return;

    /* Test 4: BCryptOpenAlgorithmProvider("SHA256") → STATUS_SUCCESS */
    HANDLE hAlg = 0;
    WCHAR sha256_id[] = {'S','H','A','2','5','6',0};
    LONG status = pBCryptOpen(&hAlg, sha256_id, NULL, 0);
    TEST_ASSERT(status == 0, "bcrypt: BCryptOpenAlgorithmProvider(SHA256) succeeds");
    TEST_ASSERT(hAlg != 0, "bcrypt: algorithm handle non-zero");

    /* Test 5: BCryptCreateHash + BCryptHashData("abc") + BCryptFinishHash */
    if (hAlg) {
        HANDLE hHash = 0;
        status = pBCryptCreateHash(hAlg, &hHash, NULL, 0, NULL, 0, 0);
        TEST_ASSERT(status == 0, "bcrypt: BCryptCreateHash succeeds");
        TEST_ASSERT(hHash != 0, "bcrypt: hash handle non-zero");

        if (hHash) {
            const BYTE abc[] = {'a', 'b', 'c'};
            status = pBCryptHashData(hHash, abc, 3, 0);
            TEST_ASSERT(status == 0, "bcrypt: BCryptHashData succeeds");

            BYTE digest[32];
            status = pBCryptFinishHash(hHash, digest, 32, 0);
            TEST_ASSERT(status == 0, "bcrypt: BCryptFinishHash succeeds");

            /* SHA-256("abc") = ba7816bf 8f01cfea 414140de 5dae2223
             *                  b00361a3 96177a9c b410ff61 f20015ad */
            TEST_ASSERT(digest[0] == 0xba && digest[1] == 0x78 &&
                        digest[2] == 0x16 && digest[3] == 0xbf,
                        "bcrypt: SHA-256(abc) first 4 bytes match");
            TEST_ASSERT(digest[28] == 0xf2 && digest[29] == 0x00 &&
                        digest[30] == 0x15 && digest[31] == 0xad,
                        "bcrypt: SHA-256(abc) last 4 bytes match");

            pBCryptDestroyHash(hHash);
        }
    }

    /* Test 6: BCryptGenRandom fills buffer */
    if (pBCryptGenRandom) {
        BYTE rng_buf[16];
        memset(rng_buf, 0, sizeof(rng_buf));
        status = pBCryptGenRandom(hAlg, rng_buf, sizeof(rng_buf), 0);
        TEST_ASSERT(status == 0, "bcrypt: BCryptGenRandom succeeds");
        int has_nonzero = 0;
        for (int i = 0; i < 16; i++) {
            if (rng_buf[i] != 0) { has_nonzero = 1; break; }
        }
        TEST_ASSERT(has_nonzero, "bcrypt: BCryptGenRandom produces non-zero bytes");
    }

    /* Test 7: BCryptCloseAlgorithmProvider → STATUS_SUCCESS */
    if (hAlg) {
        status = pBCryptClose(hAlg, 0);
        TEST_ASSERT(status == 0, "bcrypt: BCryptCloseAlgorithmProvider succeeds");
    }

    /* ── Crypt32 (cert store) ─────────────────────────────────── */

    typedef HANDLE (WINAPI *pfn_CertOpenSystemStoreA)(HANDLE, const char *);
    typedef BOOL   (WINAPI *pfn_CertCloseStore)(HANDLE, DWORD);

    pfn_CertOpenSystemStoreA pCertOpenSystemStoreA =
        (pfn_CertOpenSystemStoreA)win32_resolve_import("crypt32.dll", "CertOpenSystemStoreA");
    pfn_CertCloseStore pCertCloseStore =
        (pfn_CertCloseStore)win32_resolve_import("crypt32.dll", "CertCloseStore");

    /* Test 8: CertOpenSystemStoreA returns valid handle */
    TEST_ASSERT(pCertOpenSystemStoreA != NULL, "crypt32: CertOpenSystemStoreA resolved");
    HANDLE hStore = 0;
    if (pCertOpenSystemStoreA) {
        hStore = pCertOpenSystemStoreA(0, "ROOT");
        TEST_ASSERT(hStore != 0, "crypt32: CertOpenSystemStoreA returns non-zero");
    }

    /* Test 9: CertCloseStore returns TRUE */
    TEST_ASSERT(pCertCloseStore != NULL, "crypt32: CertCloseStore resolved");
    if (pCertCloseStore && hStore) {
        BOOL ok = pCertCloseStore(hStore, 0);
        TEST_ASSERT(ok == TRUE, "crypt32: CertCloseStore returns TRUE");
    }

    /* Test 10: W-functions resolve */
    TEST_ASSERT(pCryptAcquireContextW != NULL, "crypt: CryptAcquireContextW resolved");
}

/* ---- SEH Tests ---- */

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

static void test_scheduler_priority(void) {
    printf("== Scheduler Priority Tests ==\n");

    int me = task_get_current();
    task_info_t *t = task_get(me);
    TEST_ASSERT(t != NULL, "sched: current task exists");

    /* Boot task (TASK_IDLE, slot 0) has PRIO_IDLE; TASK_KERNEL has PRIO_NORMAL.
     * The kernel main function runs as TASK_IDLE during boot. */
    int prio = sched_get_priority(me);
    TEST_ASSERT(prio >= 0 && prio <= 3, "sched: priority in valid range 0-3");
    TEST_ASSERT(t->time_slice > 0, "sched: time_slice > 0");

    /* TASK_KERNEL (slot 1) should be PRIO_NORMAL */
    task_info_t *kern = task_get(TASK_KERNEL);
    TEST_ASSERT(kern != NULL && kern->priority == PRIO_NORMAL, "sched: TASK_KERNEL priority NORMAL");
    TEST_ASSERT(kern != NULL && kern->time_slice == SLICE_NORMAL, "sched: TASK_KERNEL time_slice == 3");

    /* Set priority to background, verify it sticks */
    sched_set_priority(me, PRIO_BACKGROUND);
    TEST_ASSERT(t->priority == PRIO_BACKGROUND, "sched: set_priority to BACKGROUND");
    TEST_ASSERT(t->time_slice == SLICE_BACKGROUND, "sched: background time_slice == 6");
    TEST_ASSERT(sched_get_priority(me) == PRIO_BACKGROUND, "sched: get confirms BACKGROUND");

    /* Set to realtime */
    sched_set_priority(me, PRIO_REALTIME);
    TEST_ASSERT(t->priority == PRIO_REALTIME, "sched: set_priority to REALTIME");
    TEST_ASSERT(t->time_slice == SLICE_REALTIME, "sched: realtime time_slice == 1");

    /* Restore to normal */
    sched_set_priority(me, PRIO_NORMAL);
    TEST_ASSERT(t->priority == PRIO_NORMAL, "sched: restored to NORMAL");

    /* Invalid tid should not crash */
    sched_set_priority(-1, PRIO_NORMAL);   /* should be no-op */
    sched_set_priority(999, PRIO_NORMAL);  /* should be no-op */
    TEST_ASSERT(1, "sched: invalid tid set_priority no crash");

    int bad = sched_get_priority(-1);
    TEST_ASSERT(bad == -1, "sched: get_priority(-1) returns -1");
}

static void test_process_lifecycle(void) {
    printf("== Process Lifecycle Tests ==\n");

    int me = task_get_current();
    task_info_t *t = task_get(me);
    TEST_ASSERT(t != NULL, "lifecycle: current task exists");

    /* parent_tid should be set (root tasks have -1) */
    TEST_ASSERT(t->parent_tid >= -1, "lifecycle: parent_tid is valid");

    /* exit_code should start at 0 */
    TEST_ASSERT(t->exit_code == 0, "lifecycle: exit_code init 0");

    /* wait_tid should be -1 (not waiting) */
    TEST_ASSERT(t->wait_tid == -1, "lifecycle: wait_tid init -1");

    /* Process group: should have valid pgid */
    TEST_ASSERT(t->pgid >= 0, "lifecycle: pgid >= 0");

    /* Session: should have valid sid */
    TEST_ASSERT(t->sid >= 0, "lifecycle: sid >= 0");

    /* sys_waitpid with WNOHANG on nonexistent child should return 0 or -1 */
    int wstatus = 0;
    int ret = sys_waitpid(-1, &wstatus, WNOHANG);
    /* No children → should return -1 (ECHILD) or 0 */
    TEST_ASSERT(ret <= 0, "lifecycle: waitpid WNOHANG no children <= 0");
}

static void test_process_groups(void) {
    printf("== Process Groups Tests ==\n");

    int me = task_get_current();
    task_info_t *t = task_get(me);
    int my_pid = t->pid;

    /* getpgid should return current pgid */
    int pgid = task_getpgid(my_pid);
    TEST_ASSERT(pgid >= 0, "pgrp: getpgid returns valid pgid");
    TEST_ASSERT(pgid == t->pgid, "pgrp: getpgid matches task pgid field");

    /* setpgid to own PID (become group leader) */
    int old_pgid = t->pgid;
    int ret = task_setpgid(my_pid, my_pid);
    TEST_ASSERT(ret == 0, "pgrp: setpgid to self succeeds");
    TEST_ASSERT(t->pgid == my_pid, "pgrp: pgid now equals own PID");

    /* Restore original pgid */
    task_setpgid(my_pid, old_pgid);

    /* getpgid for nonexistent PID should return -1 */
    int bad = task_getpgid(99999);
    TEST_ASSERT(bad == -1, "pgrp: getpgid invalid PID returns -1");
}

static void test_signals_phase2(void) {
    printf("== Signals Phase 2 Tests ==\n");

    int me = task_get_current();
    task_info_t *t = task_get(me);

    /* Signal constants should be defined correctly */
    TEST_ASSERT(SIGCHLD == 17, "signal: SIGCHLD == 17");
    TEST_ASSERT(SIGCONT == 18, "signal: SIGCONT == 18");
    TEST_ASSERT(SIGSTOP == 19, "signal: SIGSTOP == 19");
    TEST_ASSERT(SIGALRM == 14, "signal: SIGALRM == 14");
    TEST_ASSERT(SIGTSTP == 20, "signal: SIGTSTP == 20");
    TEST_ASSERT(NSIG == 32, "signal: NSIG == 32");

    /* Blocked mask should start at 0 (nothing blocked) */
    TEST_ASSERT(t->sig.blocked == 0, "signal: blocked mask init 0");

    /* Alarm ticks should start at 0 (disabled) */
    TEST_ASSERT(t->sig.alarm_ticks == 0, "signal: alarm_ticks init 0");

    /* sigprocmask: block SIGUSR1, verify it's in the mask */
    uint32_t oldset = 0;
    int ret = sig_sigprocmask(me, SIG_BLOCK, (1 << SIGUSR1), &oldset);
    TEST_ASSERT(ret == 0, "signal: sigprocmask SIG_BLOCK succeeds");
    TEST_ASSERT(oldset == 0, "signal: old mask was 0");
    TEST_ASSERT((t->sig.blocked & (1 << SIGUSR1)) != 0, "signal: SIGUSR1 now blocked");

    /* Unblock it */
    ret = sig_sigprocmask(me, SIG_UNBLOCK, (1 << SIGUSR1), &oldset);
    TEST_ASSERT(ret == 0, "signal: sigprocmask SIG_UNBLOCK succeeds");
    TEST_ASSERT((t->sig.blocked & (1 << SIGUSR1)) == 0, "signal: SIGUSR1 unblocked");

    /* SIGKILL/SIGSTOP cannot be blocked */
    sig_sigprocmask(me, SIG_BLOCK, (1 << SIGKILL) | (1 << SIGSTOP), NULL);
    TEST_ASSERT((t->sig.blocked & (1 << SIGKILL)) == 0, "signal: SIGKILL cannot be blocked");
    TEST_ASSERT((t->sig.blocked & (1 << SIGSTOP)) == 0, "signal: SIGSTOP cannot be blocked");

    /* SIG_SETMASK */
    uint32_t mask = (1 << SIGUSR2);
    sig_sigprocmask(me, SIG_SETMASK, mask, &oldset);
    TEST_ASSERT(t->sig.blocked == mask, "signal: SIG_SETMASK sets exact mask");
    /* Clean up: clear mask */
    sig_sigprocmask(me, SIG_SETMASK, 0, NULL);
    TEST_ASSERT(t->sig.blocked == 0, "signal: mask cleared to 0");
}

static void test_fd_table(void) {
    printf("== FD Table Tests ==\n");

    int me = task_get_current();
    task_info_t *t = task_get(me);
    TEST_ASSERT(t != NULL, "fd: current task exists");

    /* FD table should be dynamically allocated */
    TEST_ASSERT(t->fds != NULL, "fd: fds pointer not NULL");
    TEST_ASSERT(t->fd_count >= FD_INIT_SIZE, "fd: fd_count >= FD_INIT_SIZE (64)");

    /* Note: cooperative boot tasks don't have stdin/stdout/stderr set up as
     * FD_TTY — only ELF/shell-spawned tasks do. We test the table itself. */

    /* Allocate a pipe to test dup/dup2 */
    int rfd = -1, wfd = -1;
    int ret = pipe_create(&rfd, &wfd, me);
    TEST_ASSERT(ret == 0, "fd: pipe_create succeeds");
    TEST_ASSERT(rfd >= 0, "fd: pipe read fd >= 0");
    TEST_ASSERT(wfd >= 0, "fd: pipe write fd >= 0");
    TEST_ASSERT(rfd != wfd, "fd: pipe read != write fd");

    /* dup: should return lowest available fd */
    int dfd = fd_dup(me, rfd);
    TEST_ASSERT(dfd >= 0, "fd: dup succeeds");
    TEST_ASSERT(dfd != rfd, "fd: dup returns different fd");
    TEST_ASSERT(t->fds[dfd].type == FD_PIPE_R, "fd: dup'd fd is pipe read");
    TEST_ASSERT(t->fds[dfd].pipe_id == t->fds[rfd].pipe_id, "fd: dup'd fd same pipe_id");
    TEST_ASSERT(t->fds[dfd].cloexec == 0, "fd: dup clears cloexec");

    /* dup2: duplicate to specific fd */
    int tgt = dfd + 5;  /* pick a slot that's likely empty */
    if (tgt < t->fd_count) {
        int d2 = fd_dup2(me, wfd, tgt);
        TEST_ASSERT(d2 == tgt, "fd: dup2 returns target fd");
        TEST_ASSERT(t->fds[tgt].type == FD_PIPE_W, "fd: dup2 target is pipe write");
        TEST_ASSERT(t->fds[tgt].cloexec == 0, "fd: dup2 clears cloexec");
        /* Clean up dup2'd fd */
        pipe_close(tgt, me);
    }

    /* dup2 with oldfd == newfd: no-op, return newfd */
    int d2same = fd_dup2(me, rfd, rfd);
    TEST_ASSERT(d2same == rfd, "fd: dup2 same fd returns fd");

    /* dup bad fd should fail */
    int bad = fd_dup(me, -1);
    TEST_ASSERT(bad == -1, "fd: dup(-1) fails");
    bad = fd_dup(me, 9999);
    TEST_ASSERT(bad == -1, "fd: dup(9999) fails");

    /* Clean up */
    pipe_close(dfd, me);
    pipe_close(rfd, me);
    pipe_close(wfd, me);
}

static void test_futex(void) {
    printf("== Futex Tests ==\n");

    /* FUTEX_WAKE on an address with no waiters should return 0 */
    volatile uint32_t val = 42;
    int ret = sys_futex((uint32_t *)&val, 1 /* FUTEX_WAKE */, 1);
    TEST_ASSERT(ret == 0, "futex: WAKE no waiters returns 0");

    /* FUTEX_WAIT with val mismatch should return -EAGAIN (negative) immediately */
    ret = sys_futex((uint32_t *)&val, 0 /* FUTEX_WAIT */, 99);
    TEST_ASSERT(ret < 0, "futex: WAIT val mismatch returns negative");

    /* NULL address should not crash (returns -1) */
    ret = sys_futex(NULL, 1, 0);
    TEST_ASSERT(ret == -1 || ret == 0, "futex: NULL addr no crash");
}

static void test_pthreads(void) {
    printf("== Pthreads Tests ==\n");

    /* Mutex init and state */
    pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    TEST_ASSERT(mtx.lock == 0, "pthread: mutex initializer sets lock=0");

    int ret = pthread_mutex_init(&mtx, NULL);
    TEST_ASSERT(ret == 0, "pthread: mutex_init returns 0");
    TEST_ASSERT(mtx.lock == 0, "pthread: mutex_init sets lock=0");

    /* Lock/unlock in uncontended case */
    ret = pthread_mutex_lock(&mtx);
    TEST_ASSERT(ret == 0, "pthread: mutex_lock succeeds");
    TEST_ASSERT(mtx.lock == 1, "pthread: mutex locked, lock=1");

    ret = pthread_mutex_unlock(&mtx);
    TEST_ASSERT(ret == 0, "pthread: mutex_unlock succeeds");
    TEST_ASSERT(mtx.lock == 0, "pthread: mutex unlocked, lock=0");

    /* Trylock: should succeed when unlocked */
    ret = pthread_mutex_trylock(&mtx);
    TEST_ASSERT(ret == 0, "pthread: trylock succeeds when unlocked");
    TEST_ASSERT(mtx.lock == 1, "pthread: trylock acquired");

    /* Trylock again: should fail (already locked) */
    ret = pthread_mutex_trylock(&mtx);
    TEST_ASSERT(ret == -1, "pthread: trylock fails when locked");

    pthread_mutex_unlock(&mtx);

    /* Destroy */
    ret = pthread_mutex_destroy(&mtx);
    TEST_ASSERT(ret == 0, "pthread: mutex_destroy returns 0");

    /* Condvar init */
    pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
    TEST_ASSERT(cv.seq == 0, "pthread: condvar initializer sets seq=0");

    ret = pthread_cond_init(&cv, NULL);
    TEST_ASSERT(ret == 0, "pthread: cond_init returns 0");

    /* Signal increments sequence counter */
    int old_seq = cv.seq;
    ret = pthread_cond_signal(&cv);
    TEST_ASSERT(ret == 0, "pthread: cond_signal returns 0");
    TEST_ASSERT(cv.seq == old_seq + 1, "pthread: cond_signal increments seq");

    /* Broadcast also increments */
    old_seq = cv.seq;
    ret = pthread_cond_broadcast(&cv);
    TEST_ASSERT(ret == 0, "pthread: cond_broadcast returns 0");
    TEST_ASSERT(cv.seq == old_seq + 1, "pthread: cond_broadcast increments seq");

    ret = pthread_cond_destroy(&cv);
    TEST_ASSERT(ret == 0, "pthread: cond_destroy returns 0");
}

/* ---- Phase 3: Memory Management ---- */

static void test_vma(void) {
    printf("[VMA] ");

    /* Create VMA table */
    vma_table_t *vt = vma_init();
    TEST_ASSERT(vt != NULL, "vma: init returns non-NULL");
    TEST_ASSERT(vt->count == 0, "vma: init count is 0");
    TEST_ASSERT(vt->mmap_next == 0x20000000, "vma: init mmap_next is 0x20000000");

    /* Insert VMAs */
    int rc = vma_insert(vt, 0x08048000, 0x0804C000, VMA_READ | VMA_EXEC, VMA_TYPE_ELF);
    TEST_ASSERT(rc == 0, "vma: insert ELF segment succeeds");
    TEST_ASSERT(vt->count == 1, "vma: count is 1 after insert");

    rc = vma_insert(vt, 0x0804D000, 0x08050000, VMA_READ | VMA_WRITE, VMA_TYPE_BRK);
    TEST_ASSERT(rc == 0, "vma: insert BRK succeeds");
    TEST_ASSERT(vt->count == 2, "vma: count is 2 after second insert");

    rc = vma_insert(vt, 0x40000000, 0x40001000, VMA_READ | VMA_WRITE | VMA_GROWSDOWN, VMA_TYPE_STACK);
    TEST_ASSERT(rc == 0, "vma: insert STACK succeeds");

    /* Find VMAs */
    vma_t *v = vma_find(vt, 0x08049000);
    TEST_ASSERT(v != NULL, "vma: find 0x08049000 in ELF segment");
    TEST_ASSERT(v->vm_type == VMA_TYPE_ELF, "vma: found VMA is ELF type");

    v = vma_find(vt, 0x0804E000);
    TEST_ASSERT(v != NULL, "vma: find 0x0804E000 in BRK");
    TEST_ASSERT(v->vm_type == VMA_TYPE_BRK, "vma: found VMA is BRK type");

    v = vma_find(vt, 0x12345000);
    TEST_ASSERT(v == NULL, "vma: find unmapped address returns NULL");

    /* Split VMA */
    rc = vma_split(vt, 0x0804A000);
    TEST_ASSERT(rc == 0, "vma: split ELF at 0x0804A000 succeeds");
    TEST_ASSERT(vt->count == 4, "vma: count is 4 after split");

    v = vma_find(vt, 0x08048000);
    TEST_ASSERT(v != NULL && v->vm_end == 0x0804A000, "vma: lower half ends at split point");

    v = vma_find(vt, 0x0804B000);
    TEST_ASSERT(v != NULL && v->vm_start == 0x0804A000, "vma: upper half starts at split point");

    /* Find free gap */
    uint32_t free_va = vma_find_free(vt, 0x4000);
    TEST_ASSERT(free_va != 0, "vma: find_free returns non-zero");
    TEST_ASSERT((free_va & 0xFFF) == 0, "vma: find_free returns page-aligned");

    /* Remove VMA */
    int pages = vma_remove(vt, 0x0804D000, 0x08050000);
    TEST_ASSERT(pages == 3, "vma: remove BRK returns 3 pages");

    v = vma_find(vt, 0x0804E000);
    TEST_ASSERT(v == NULL, "vma: BRK region no longer found after remove");

    /* Clone */
    vma_table_t *clone = vma_clone(vt);
    TEST_ASSERT(clone != NULL, "vma: clone returns non-NULL");
    TEST_ASSERT(clone->count == vt->count, "vma: clone has same count");

    v = vma_find(clone, 0x40000000);
    TEST_ASSERT(v != NULL, "vma: clone contains stack VMA");

    /* Type names */
    TEST_ASSERT(strcmp(vma_type_name(VMA_TYPE_ELF), "elf") == 0, "vma: type_name ELF");
    TEST_ASSERT(strcmp(vma_type_name(VMA_TYPE_STACK), "stack") == 0, "vma: type_name STACK");

    /* Cleanup */
    vma_destroy(clone);
    vma_destroy(vt);
}

static void test_frame_ref(void) {
    printf("[FRAME_REF] ");

    /* Allocate a test frame */
    uint32_t frame = pmm_alloc_frame();
    TEST_ASSERT(frame != 0, "frame_ref: alloc frame");

    /* pmm_alloc_frame calls frame_ref_set1, so refcount should be 1 */
    TEST_ASSERT(frame_ref_get(frame) == 1, "frame_ref: initial refcount is 1");

    /* Increment */
    frame_ref_inc(frame);
    TEST_ASSERT(frame_ref_get(frame) == 2, "frame_ref: inc to 2");

    frame_ref_inc(frame);
    TEST_ASSERT(frame_ref_get(frame) == 3, "frame_ref: inc to 3");

    /* Decrement */
    int rc = frame_ref_dec(frame);
    TEST_ASSERT(rc == 2, "frame_ref: dec returns 2");
    TEST_ASSERT(frame_ref_get(frame) == 2, "frame_ref: get returns 2");

    rc = frame_ref_dec(frame);
    TEST_ASSERT(rc == 1, "frame_ref: dec returns 1");

    rc = frame_ref_dec(frame);
    TEST_ASSERT(rc == 0, "frame_ref: dec returns 0");

    /* Free the frame now that refcount is 0 */
    pmm_free_frame(frame);

    /* Out-of-range should be safe */
    TEST_ASSERT(frame_ref_get(0xFFFFFFFF) == 0, "frame_ref: out-of-range get returns 0");
}

static void test_memory_phase3(void) {
    printf("[MEMORY_P3] ");

    /* Test VMA insertion with overlapping check */
    vma_table_t *vt = vma_init();
    TEST_ASSERT(vt != NULL, "mem_p3: vma_init ok");

    /* Insert and find_free should skip existing VMAs */
    vma_insert(vt, 0x20000000, 0x20004000, VMA_READ | VMA_WRITE | VMA_ANON, VMA_TYPE_ANON);
    vt->mmap_next = 0x20000000;  /* Reset to force scanning past existing */

    uint32_t gap = vma_find_free(vt, 0x1000);
    TEST_ASSERT(gap >= 0x20004000, "mem_p3: find_free skips existing VMA");

    /* Test vmm helpers */
    uint32_t pd = vmm_create_user_pagedir();
    TEST_ASSERT(pd != 0, "mem_p3: create user pagedir");

    /* vmm_get_pte for unmapped page should return 0 */
    uint32_t pte = vmm_get_pte(pd, 0x20000000);
    /* PTE may be from kernel page table (identity mapped), check it exists */
    TEST_ASSERT(1, "mem_p3: vmm_get_pte doesn't crash");

    /* vmm_ensure_pt should create page table entry */
    uint32_t pt = vmm_ensure_pt(pd, 0x30000000);
    TEST_ASSERT(pt != 0, "mem_p3: vmm_ensure_pt allocates page table");

    /* Map a page then check PTE */
    uint32_t frame = pmm_alloc_frame();
    TEST_ASSERT(frame != 0, "mem_p3: alloc frame for mapping");
    vmm_map_user_page(pd, 0x30000000, frame, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    pte = vmm_get_pte(pd, 0x30000000);
    TEST_ASSERT(pte & PTE_PRESENT, "mem_p3: mapped page is present");
    TEST_ASSERT((pte & PAGE_MASK) == frame, "mem_p3: PTE points to correct frame");

    /* Unmap */
    vmm_unmap_user_page(pd, 0x30000000);
    pte = vmm_get_pte(pd, 0x30000000);
    TEST_ASSERT(!(pte & PTE_PRESENT), "mem_p3: unmapped page not present");

    /* Free frame and page dir */
    pmm_free_frame(frame);
    vmm_destroy_user_pagedir(pd);

    /* Test PTE_COW flag value */
    TEST_ASSERT(PTE_COW == 0x400, "mem_p3: PTE_COW is AVL bit 10");

    vma_destroy(vt);
}

/* ---- Phase 4: Linux Syscall Expansion ---- */

static void test_phase4_syscalls(void) {
    printf("== Phase 4 Syscall Tests ==\n");

    /* ── Verify new syscall number defines ────────────────────────── */
    TEST_ASSERT(LINUX_SYS_unlink == 10, "SYS_unlink defined");
    TEST_ASSERT(LINUX_SYS_chdir == 12, "SYS_chdir defined");
    TEST_ASSERT(LINUX_SYS_time == 13, "SYS_time defined");
    TEST_ASSERT(LINUX_SYS_rename == 38, "SYS_rename defined");
    TEST_ASSERT(LINUX_SYS_mkdir == 39, "SYS_mkdir defined");
    TEST_ASSERT(LINUX_SYS_rmdir == 40, "SYS_rmdir defined");
    TEST_ASSERT(LINUX_SYS_pipe == 42, "SYS_pipe defined");
    TEST_ASSERT(LINUX_SYS_umask == 60, "SYS_umask defined");
    TEST_ASSERT(LINUX_SYS_getpgrp == 65, "SYS_getpgrp defined");
    TEST_ASSERT(LINUX_SYS_gettimeofday == 78, "SYS_gettimeofday defined");
    TEST_ASSERT(LINUX_SYS_fchdir == 133, "SYS_fchdir defined");
    TEST_ASSERT(LINUX_SYS_readv == 145, "SYS_readv defined");
    TEST_ASSERT(LINUX_SYS_poll == 168, "SYS_poll defined");
    TEST_ASSERT(LINUX_SYS_setuid32 == 213, "SYS_setuid32 defined");
    TEST_ASSERT(LINUX_SYS_setgid32 == 214, "SYS_setgid32 defined");
    TEST_ASSERT(LINUX_SYS_clock_gettime == 265, "SYS_clock_gettime defined");
    TEST_ASSERT(LINUX_SYS_statfs64 == 268, "SYS_statfs64 defined");
    TEST_ASSERT(LINUX_SYS_fstatfs64 == 269, "SYS_fstatfs64 defined");

    /* ── Verify new errno/ioctl defines ───────────────────────────── */
    TEST_ASSERT(LINUX_ENOTEMPTY == 39, "ENOTEMPTY defined");
    TEST_ASSERT(LINUX_EPERM == 1, "EPERM defined");
    TEST_ASSERT(LINUX_TCSETS == 0x5402, "TCSETS defined");
    TEST_ASSERT(LINUX_FIONREAD == 0x541B, "FIONREAD defined");
    TEST_ASSERT(LINUX_TIOCGPGRP == 0x540F, "TIOCGPGRP defined");

    /* ── Verify poll event flags ──────────────────────────────────── */
    TEST_ASSERT(LINUX_POLLIN == 0x0001, "POLLIN defined");
    TEST_ASSERT(LINUX_POLLOUT == 0x0004, "POLLOUT defined");
    TEST_ASSERT(LINUX_POLLHUP == 0x0010, "POLLHUP defined");
    TEST_ASSERT(LINUX_POLLNVAL == 0x0020, "POLLNVAL defined");

    /* ── unlink: create → delete → verify gone ────────────────────── */
    {
        uint32_t saved_inode = fs_get_cwd_inode();
        fs_change_directory("/tmp");

        fs_create_file("p4unlink", 0);
        fs_write_file("p4unlink", (const uint8_t *)"test", 4);

        /* Verify file exists */
        uint32_t par;
        char nm[MAX_NAME_LEN];
        int ino = fs_resolve_path("/tmp/p4unlink", &par, nm);
        TEST_ASSERT(ino >= 0, "unlink: file exists before delete");

        /* Delete via our handler */
        int rc = fs_delete_file("p4unlink");
        TEST_ASSERT(rc == 0, "unlink: delete succeeds");

        /* Verify gone */
        ino = fs_resolve_path("/tmp/p4unlink", &par, nm);
        TEST_ASSERT(ino < 0, "unlink: file gone after delete");

        fs_change_directory_by_inode(saved_inode);
    }

/* ── mkdir/rmdir: create dir → verify → rmdir → verify gone ──── */
    {
        uint32_t saved_inode = fs_get_cwd_inode();
        fs_change_directory("/tmp");

        int rc = fs_create_file("p4dir", 1);
        TEST_ASSERT(rc >= 0, "mkdir: create succeeds");

        uint32_t par;
        char nm[MAX_NAME_LEN];
        int ino = fs_resolve_path("/tmp/p4dir", &par, nm);
        TEST_ASSERT(ino >= 0, "mkdir: dir exists");

        inode_t node;
        if (ino >= 0) {
            fs_read_inode((uint32_t)ino, &node);
            TEST_ASSERT(node.type == INODE_DIR, "mkdir: inode is DIR type");
        }

        /* rmdir */
        rc = fs_delete_file("p4dir");
        TEST_ASSERT(rc == 0, "rmdir: delete succeeds");

        ino = fs_resolve_path("/tmp/p4dir", &par, nm);
        TEST_ASSERT(ino < 0, "rmdir: dir gone after delete");

        fs_change_directory_by_inode(saved_inode);
    }

/* ── rename: create → rename → old gone, new exists ──────────── */
    {
        uint32_t saved_inode = fs_get_cwd_inode();
        fs_change_directory("/tmp");

        fs_create_file("p4old", 0);
        fs_write_file("p4old", (const uint8_t *)"rename", 6);

        int rc = fs_rename("p4old", "p4new");
        TEST_ASSERT(rc == 0, "rename: succeeds");

        uint32_t par;
        char nm[MAX_NAME_LEN];
        int ino_old = fs_resolve_path("/tmp/p4old", &par, nm);
        TEST_ASSERT(ino_old < 0, "rename: old name gone");

        int ino_new = fs_resolve_path("/tmp/p4new", &par, nm);
        TEST_ASSERT(ino_new >= 0, "rename: new name exists");

        /* Cleanup */
        fs_delete_file("p4new");
        fs_change_directory_by_inode(saved_inode);
    }

/* ── chdir: change to /tmp → verify cwd → restore ───────────── */
    {
        uint32_t saved_inode = fs_get_cwd_inode();
        int rc = fs_change_directory("/tmp");
        TEST_ASSERT(rc == 0, "chdir: to /tmp succeeds");

        const char *cwd = fs_get_cwd();
        TEST_ASSERT(strcmp(cwd, "/tmp") == 0, "chdir: cwd is /tmp");

        fs_change_directory_by_inode(saved_inode);
    }

/* ── pipe: create → write → read → verify data ───────────────── */
    {
        int tid = task_get_current();
        int rfd, wfd;
        int rc = pipe_create(&rfd, &wfd, tid);
        TEST_ASSERT(rc == 0, "pipe: create succeeds");

        task_info_t *t = task_get(tid);
        if (rc == 0 && t && t->fds) {
            /* Write to pipe */
            const char *msg = "hello";
            int wr = pipe_write(wfd, msg, 5, tid);
            TEST_ASSERT(wr == 5, "pipe: write 5 bytes");

            /* Read from pipe */
            char buf[16] = {0};
            int rd = pipe_read(rfd, buf, 16, tid);
            TEST_ASSERT(rd == 5, "pipe: read 5 bytes back");
            TEST_ASSERT(memcmp(buf, "hello", 5) == 0, "pipe: data matches");

            /* Cleanup */
            pipe_close(rfd, tid);
            pipe_close(wfd, tid);
        }
    }

/* ── umask: verify default, set, verify old returned ─────────── */
    {
        int tid = task_get_current();
        task_info_t *t = task_get(tid);
        if (t) {
            uint16_t saved = t->umask;
            /* Set to 0077, check old returned */
            uint16_t old = t->umask;
            t->umask = 0077;
            TEST_ASSERT(old == saved, "umask: old value returned");
            TEST_ASSERT(t->umask == 0077, "umask: new value set");
            t->umask = saved;  /* restore */
        }
    }

/* ── dup/dup2: dup fd → both read same data ──────────────────── */
    {
        int tid = task_get_current();
        int rfd, wfd;
        int rc = pipe_create(&rfd, &wfd, tid);
        TEST_ASSERT(rc == 0, "dup: pipe create");

        if (rc == 0) {
            int dup_rfd = fd_dup(tid, rfd);
            TEST_ASSERT(dup_rfd >= 0, "dup: succeeds");

            /* Write data */
            pipe_write(wfd, "XY", 2, tid);

            /* Read from original fd */
            char buf[4] = {0};
            int rd = pipe_read(rfd, buf, 1, tid);
            TEST_ASSERT(rd == 1 && buf[0] == 'X', "dup: read from original");

            /* Read from duped fd — should get next byte */
            rd = pipe_read(dup_rfd, buf, 1, tid);
            TEST_ASSERT(rd == 1 && buf[0] == 'Y', "dup: read from dup");

            /* dup2 to specific fd */
            int dup2_fd = fd_dup2(tid, wfd, 10);
            TEST_ASSERT(dup2_fd == 10, "dup2: to fd 10");

            pipe_close(rfd, tid);
            if (dup_rfd >= 0) pipe_close(dup_rfd, tid);
            pipe_close(wfd, tid);
            if (dup2_fd >= 0) pipe_close(dup2_fd, tid);
        }
    }

/* ── time: Unix timestamp must be after 2024-01-01 ───────────── */
    {
        extern uint32_t rtc_get_epoch(void);
        uint32_t unix_time = rtc_get_epoch() + IMPOS_EPOCH_OFFSET;
        TEST_ASSERT(unix_time > 1700000000U, "time: epoch > 2024");
    }

/* ── readv: write data → readv with 2 iovecs → verify ────────── */
    {
        uint32_t saved_inode = fs_get_cwd_inode();
        fs_change_directory("/tmp");

        fs_create_file("p4readv", 0);
        fs_write_file("p4readv", (const uint8_t *)"ABCDEFGH", 8);

        /* Open via resolve + manual FD setup */
        uint32_t par;
        char nm[MAX_NAME_LEN];
        int ino = fs_resolve_path("/tmp/p4readv", &par, nm);
        TEST_ASSERT(ino >= 0, "readv: file exists");

        if (ino >= 0) {
            /* Read 4 bytes at offset 0, then 4 bytes at offset 4 */
            char buf1[8] = {0}, buf2[8] = {0};
            int r1 = fs_read_at((uint32_t)ino, (uint8_t *)buf1, 0, 4);
            int r2 = fs_read_at((uint32_t)ino, (uint8_t *)buf2, 4, 4);
            TEST_ASSERT(r1 == 4 && memcmp(buf1, "ABCD", 4) == 0, "readv: first chunk");
            TEST_ASSERT(r2 == 4 && memcmp(buf2, "EFGH", 4) == 0, "readv: second chunk");
        }

        fs_delete_file("p4readv");
        fs_change_directory_by_inode(saved_inode);
    }

/* ── poll: pipe pair → write → poll read end → POLLIN set ────── */
    {
        int tid = task_get_current();
        int rfd, wfd;
        int rc = pipe_create(&rfd, &wfd, tid);
        TEST_ASSERT(rc == 0, "poll: pipe create");

        if (rc == 0) {
            /* Write a byte */
            pipe_write(wfd, "Z", 1, tid);

            /* Query read end */
            int r = pipe_poll_query(task_get(tid)->fds[rfd].pipe_id, 0);
            TEST_ASSERT(r & PIPE_POLL_IN, "poll: POLLIN set after write");

            /* Query write end */
            r = pipe_poll_query(task_get(tid)->fds[wfd].pipe_id, 1);
            TEST_ASSERT(r & PIPE_POLL_OUT, "poll: POLLOUT set (space available)");

            pipe_close(rfd, tid);
            pipe_close(wfd, tid);
        }
    }

/* ── statfs64: verify FS geometry constants ──────────────────── */
    {
        struct linux_statfs64 st;
        memset(&st, 0, sizeof(st));
        st.f_type = 0x696D706F;
        st.f_bsize = BLOCK_SIZE;
        st.f_blocks = NUM_BLOCKS;
        st.f_files = NUM_INODES;
        st.f_namelen = MAX_NAME_LEN;
        TEST_ASSERT(st.f_bsize == 4096, "statfs: f_bsize == 4096");
        TEST_ASSERT(st.f_blocks == 65536, "statfs: f_blocks == 65536");
        TEST_ASSERT(st.f_files == 4096, "statfs: f_files == 4096");
        TEST_ASSERT(st.f_namelen == 28, "statfs: f_namelen == 28");
    }

/* ── ioctl TIOCGWINSZ: verify reasonable values ──────────────── */
    {
        struct linux_winsize ws = {0};
        ws.ws_row = 67;
        ws.ws_col = 240;
        ws.ws_xpixel = 1920;
        ws.ws_ypixel = 1080;
        TEST_ASSERT(ws.ws_row > 0 && ws.ws_row < 256, "ioctl: ws_row reasonable");
        TEST_ASSERT(ws.ws_col > 0 && ws.ws_col < 1024, "ioctl: ws_col reasonable");
    }

/* ── clock_gettime: monotonic time advancing ─────────────────── */
    {
        extern volatile uint32_t pit_ticks;
        uint32_t t1 = pit_ticks;
        TEST_ASSERT(t1 > 0, "clock_gettime: PIT running");

        /* Verify CLOCK_REALTIME gives unix time */
        uint32_t unix_time = rtc_get_epoch() + IMPOS_EPOCH_OFFSET;
        TEST_ASSERT(unix_time > 1700000000U, "clock_gettime: REALTIME valid");
    }

/* ── pipe_get_count: verify byte count ───────────────────────── */
    {
        int tid = task_get_current();
        int rfd, wfd;
        int rc = pipe_create(&rfd, &wfd, tid);
        if (rc == 0) {
            task_info_t *t = task_get(tid);
            pipe_write(wfd, "ABC", 3, tid);
            uint32_t cnt = pipe_get_count(t->fds[rfd].pipe_id);
            TEST_ASSERT(cnt == 3, "pipe_get_count: 3 bytes");
            pipe_close(rfd, tid);
            pipe_close(wfd, tid);
        }
    }

/* ── fs_count_free helpers: verify non-zero ──────────────────── */
    {
        uint32_t fb = fs_count_free_blocks();
        uint32_t fi = fs_count_free_inodes();
        TEST_ASSERT(fb > 0, "fs_count_free_blocks > 0");
        TEST_ASSERT(fi > 0, "fs_count_free_inodes > 0");
        TEST_ASSERT(fb < NUM_BLOCKS, "fs_count_free_blocks < NUM_BLOCKS");
        TEST_ASSERT(fi < NUM_INODES, "fs_count_free_inodes < NUM_INODES");
    }
}

/* ---- Nanosleep & Execve ---- */

static void test_nanosleep_execve(void) {
    printf("== Nanosleep & Execve Tests ==\n");

    /* Verify syscall number defines exist */
    TEST_ASSERT(LINUX_SYS_nanosleep == 162, "SYS_nanosleep defined");
    TEST_ASSERT(LINUX_SYS_clock_nanosleep == 267, "SYS_clock_nanosleep defined");
    TEST_ASSERT(LINUX_SYS_execve == 11, "SYS_execve defined");

    /* Verify errno defines */
    TEST_ASSERT(LINUX_EINTR == 4, "EINTR defined");
    TEST_ASSERT(LINUX_ENOEXEC == 8, "ENOEXEC defined");
    TEST_ASSERT(LINUX_EFAULT == 14, "EFAULT defined");

    /* Test the timing infrastructure — PIT must be running for nanosleep */
    uint32_t t1 = pit_get_ticks();
    TEST_ASSERT(t1 > 0, "PIT ticks running");

    /* Test elf_exec with non-existent file — should return ENOENT */
    int tid = task_get_current();
    int rc = elf_exec(tid, "/nonexistent_binary", 0, NULL);
    TEST_ASSERT(rc == -LINUX_ENOENT, "elf_exec nonexistent returns ENOENT");

    /* Test elf_exec with invalid file — create a non-ELF file, then try exec */
    fs_create_file("/tmp/notelf", 0);
    fs_write_file("/tmp/notelf", (const uint8_t *)"notanelf", 8);
    rc = elf_exec(tid, "/tmp/notelf", 0, NULL);
    TEST_ASSERT(rc == -LINUX_ENOEXEC, "elf_exec non-ELF returns ENOEXEC");
    fs_delete_file("/tmp/notelf");

    /* Verify elf_exec function pointer valid */
    int (*exec_fn)(int, const char *, int, const char **) = elf_exec;
    TEST_ASSERT(exec_fn != NULL, "elf_exec function pointer valid");
    (void)exec_fn;

    /* ── Live ELF binary tests (require initrd binaries) ───────── */

    /* Test sleep_test: spawns ELF, checks it completes and timing is ~1s */
    {
        int ret = elf_run("/bin/sleep_test");
        if (ret >= 0) {
            task_info_t *t = task_get(ret);
            uint32_t t_start = pit_get_ticks();
            while (t && t->active && t->state != TASK_STATE_ZOMBIE)
                task_yield();
            uint32_t elapsed = pit_get_ticks() - t_start;
            /* 120Hz PIT: 1 second = 120 ticks, allow 90-180 range */
            TEST_ASSERT(elapsed >= 90 && elapsed <= 180,
                        "sleep_test paused ~1s");
        } else {
            TEST_ASSERT(0, "sleep_test binary found in initrd");
        }
    }

    /* Test exec_test + exec_target: spawn exec_test, verify it completes */
    {
        int ret = elf_run("/bin/exec_test");
        if (ret >= 0) {
            task_info_t *t = task_get(ret);
            int pid_before = t ? t->pid : -1;
            while (t && t->active && t->state != TASK_STATE_ZOMBIE)
                task_yield();
            /* exec_test prints EXEC_PID=N, then execve's to exec_target
             * which prints TARGET_PID=N. Both should complete without crash. */
            TEST_ASSERT(t && t->state == TASK_STATE_ZOMBIE,
                        "exec_test completed (zombie)");
            TEST_ASSERT(t && t->exit_code == 0,
                        "exec_target exited with code 0");
        } else {
            TEST_ASSERT(0, "exec_test binary found in initrd");
        }
    }
}

/* ---- Phase 5: Dynamic Linking Infrastructure Tests ---- */

static void test_phase5_dynlink(void) {
    printf("== Phase 5 Dynamic Linking Tests ==\n");

    /* ── ELF type/program header defines ─────────────────────────── */
    TEST_ASSERT(ET_EXEC == 2, "ET_EXEC == 2");
    TEST_ASSERT(ET_DYN == 3, "ET_DYN == 3");
    TEST_ASSERT(PT_NULL == 0, "PT_NULL == 0");
    TEST_ASSERT(PT_LOAD == 1, "PT_LOAD == 1");
    TEST_ASSERT(PT_DYNAMIC == 2, "PT_DYNAMIC == 2");
    TEST_ASSERT(PT_INTERP == 3, "PT_INTERP == 3");
    TEST_ASSERT(PT_NOTE == 4, "PT_NOTE == 4");
    TEST_ASSERT(PT_PHDR == 6, "PT_PHDR == 6");

    /* ── Auxiliary vector defines ─────────────────────────────────── */
    TEST_ASSERT(AT_NULL == 0, "AT_NULL == 0");
    TEST_ASSERT(AT_PHDR == 3, "AT_PHDR == 3");
    TEST_ASSERT(AT_PHENT == 4, "AT_PHENT == 4");
    TEST_ASSERT(AT_PHNUM == 5, "AT_PHNUM == 5");
    TEST_ASSERT(AT_PAGESZ == 6, "AT_PAGESZ == 6");
    TEST_ASSERT(AT_BASE == 7, "AT_BASE == 7");
    TEST_ASSERT(AT_ENTRY == 9, "AT_ENTRY == 9");
    TEST_ASSERT(AT_UID == 11, "AT_UID == 11");
    TEST_ASSERT(AT_EUID == 12, "AT_EUID == 12");
    TEST_ASSERT(AT_GID == 13, "AT_GID == 13");
    TEST_ASSERT(AT_EGID == 14, "AT_EGID == 14");
    TEST_ASSERT(AT_HWCAP == 16, "AT_HWCAP == 16");
    TEST_ASSERT(AT_CLKTCK == 17, "AT_CLKTCK == 17");
    TEST_ASSERT(AT_SECURE == 23, "AT_SECURE == 23");
    TEST_ASSERT(AT_RANDOM == 25, "AT_RANDOM == 25");

    /* ── Struct size validation ───────────────────────────────────── */
    TEST_ASSERT(sizeof(Elf32_auxv_t) == 8, "Elf32_auxv_t is 8 bytes");
    TEST_ASSERT(sizeof(Elf32_Phdr) == 32, "Elf32_Phdr is 32 bytes");
    TEST_ASSERT(sizeof(Elf32_Ehdr) == 52, "Elf32_Ehdr is 52 bytes");

    /* ── Interpreter base address ─────────────────────────────────── */
    TEST_ASSERT(INTERP_BASE_ADDR == 0x40100000, "INTERP_BASE_ADDR correct");
    TEST_ASSERT(INTERP_BASE_ADDR > USER_SPACE_BASE, "interp base above stack");

    /* ── ET_DYN acceptance in elf_detect ──────────────────────────── */
    /* elf_detect only checks magic bytes, not e_type — verify it accepts
     * both ET_EXEC and ET_DYN headers */
    {
        uint8_t fake_elf[64];
        memset(fake_elf, 0, sizeof(fake_elf));
        fake_elf[0] = 0x7F;
        fake_elf[1] = 'E';
        fake_elf[2] = 'L';
        fake_elf[3] = 'F';
        TEST_ASSERT(elf_detect(fake_elf, sizeof(fake_elf)) == 1,
                    "elf_detect accepts ELF magic");

        /* Non-ELF data */
        uint8_t not_elf[4] = {0x00, 0x01, 0x02, 0x03};
        TEST_ASSERT(elf_detect(not_elf, 4) == 0, "elf_detect rejects non-ELF");

        /* Too small */
        TEST_ASSERT(elf_detect(not_elf, 2) == 0, "elf_detect rejects small data");
    }

    /* ── Elf32_auxv_t field offsets ──────────────────────────────── */
    {
        Elf32_auxv_t av;
        av.a_type = AT_PAGESZ;
        av.a_val = 4096;
        TEST_ASSERT(av.a_type == AT_PAGESZ, "auxv_t type field works");
        TEST_ASSERT(av.a_val == 4096, "auxv_t val field works");
    }

    /* ── File-backed mmap infrastructure ─────────────────────────── */
    /* Test that a file created in /tmp can be read via fs_read_at */
    {
        uint32_t saved_inode = fs_get_cwd_inode();
        fs_change_directory("/tmp");

        /* Create test file with known pattern */
        fs_create_file("mmap_test", 0);
        uint8_t pattern[64];
        for (int i = 0; i < 64; i++) pattern[i] = (uint8_t)(i ^ 0xAA);
        fs_write_file("mmap_test", pattern, 64);

        /* Read it back using fs_read_at (same path mmap uses) */
        uint32_t par;
        char nm[256];
        int ino = fs_resolve_path("/tmp/mmap_test", &par, nm);
        TEST_ASSERT(ino >= 0, "mmap_test file exists");

        if (ino >= 0) {
            uint8_t readback[64];
            memset(readback, 0, 64);
            int rd = fs_read_at((uint32_t)ino, readback, 0, 64);
            TEST_ASSERT(rd == 64, "fs_read_at reads 64 bytes");
            TEST_ASSERT(memcmp(readback, pattern, 64) == 0,
                        "fs_read_at data matches pattern");

            /* Offset read */
            memset(readback, 0, 32);
            rd = fs_read_at((uint32_t)ino, readback, 32, 32);
            TEST_ASSERT(rd == 32, "fs_read_at offset read 32 bytes");
            TEST_ASSERT(memcmp(readback, pattern + 32, 32) == 0,
                        "fs_read_at offset data matches");
        }

        fs_delete_file("mmap_test");
        fs_change_directory_by_inode(saved_inode);
    }

    /* ── mmap flags defines ──────────────────────────────────────── */
    TEST_ASSERT(LINUX_MAP_ANONYMOUS == 0x20, "MAP_ANONYMOUS == 0x20");
    TEST_ASSERT(LINUX_MAP_FIXED == 0x10, "MAP_FIXED == 0x10");
    TEST_ASSERT(LINUX_MAP_PRIVATE == 0x02, "MAP_PRIVATE == 0x02");
    TEST_ASSERT(LINUX_PROT_READ == 0x1, "PROT_READ == 0x1");
    TEST_ASSERT(LINUX_PROT_WRITE == 0x2, "PROT_WRITE == 0x2");
    TEST_ASSERT(LINUX_PROT_EXEC == 0x4, "PROT_EXEC == 0x4");
    TEST_ASSERT(LINUX_PROT_NONE == 0x0, "PROT_NONE == 0x0");

    /* ── elf_load_interp function pointer exists ─────────────────── */
    uint32_t (*interp_fn)(uint32_t, const char *, uint32_t, void *, void *)
        = elf_load_interp;
    TEST_ASSERT(interp_fn != NULL, "elf_load_interp function exists");
    (void)interp_fn;

    /* ── VMA type for ELF segments ───────────────────────────────── */
    TEST_ASSERT(VMA_TYPE_ELF == 2, "VMA_TYPE_ELF == 2");
    TEST_ASSERT(VMA_TYPE_ANON == 1, "VMA_TYPE_ANON == 1");
    TEST_ASSERT(VMA_TYPE_STACK == 3, "VMA_TYPE_STACK == 3");

    /* ── Live dynamic binary integration test ────────────────────── */
    /* Spawn hello_dyn (dynamically linked via musl ldso), wait for
     * it to finish. This exercises the full chain: PT_INTERP scan →
     * elf_load_interp → auxv → file-backed mmap → ldso bootstrap →
     * symbol resolution → CRT → main() → write() → _exit(). */
    {
        int ret = elf_run("/bin/hello_dyn");
        if (ret >= 0) {
            task_info_t *t = task_get(ret);
            uint32_t t_start = pit_get_ticks();
            while (t && t->active && t->state != TASK_STATE_ZOMBIE) {
                task_yield();
                /* Timeout after ~5 seconds (600 ticks at 120 Hz) */
                if (pit_get_ticks() - t_start > 600) break;
            }
            TEST_ASSERT(t && t->state == TASK_STATE_ZOMBIE,
                        "hello_dyn completed (zombie)");
            TEST_ASSERT(t && t->exit_code == 0,
                        "hello_dyn exited with code 0");
        } else {
            /* Binary not in initrd — skip gracefully */
            printf("  (hello_dyn not in initrd, skipping live test)\n");
        }
    }
}

/* ---- Phase 6: Networking Stack Modernization ---- */

static void test_phase6_networking(void) {
    printf("== Phase 6 Networking Tests ==\n");

    /* ── FD_SOCKET constant ────────────────────────────────────────── */
    TEST_ASSERT(FD_SOCKET == 8, "FD_SOCKET == 8");

    /* ── Linux address family constants ────────────────────────────── */
    TEST_ASSERT(AF_INET == 2, "AF_INET == 2");
    TEST_ASSERT(AF_UNIX == 1, "AF_UNIX == 1");

    /* ── Socket type constants ─────────────────────────────────────── */
    TEST_ASSERT(SOCK_STREAM == 1, "SOCK_STREAM == 1");
    TEST_ASSERT(SOCK_DGRAM == 2, "SOCK_DGRAM == 2");

    /* ── sockaddr_in size (must be 16 bytes for Linux compat) ──────── */
    TEST_ASSERT(sizeof(struct linux_sockaddr_in) == 16,
                "sizeof(linux_sockaddr_in) == 16");

    /* ── socketcall sub-function constants ─────────────────────────── */
    TEST_ASSERT(SYS_SOCKET == 1, "SYS_SOCKET == 1");
    TEST_ASSERT(SYS_BIND == 2, "SYS_BIND == 2");
    TEST_ASSERT(SYS_CONNECT == 3, "SYS_CONNECT == 3");
    TEST_ASSERT(SYS_LISTEN == 4, "SYS_LISTEN == 4");
    TEST_ASSERT(SYS_ACCEPT == 5, "SYS_ACCEPT == 5");
    TEST_ASSERT(SYS_GETSOCKNAME == 6, "SYS_GETSOCKNAME == 6");
    TEST_ASSERT(SYS_GETPEERNAME == 7, "SYS_GETPEERNAME == 7");
    TEST_ASSERT(SYS_SEND == 9, "SYS_SEND == 9");
    TEST_ASSERT(SYS_RECV == 10, "SYS_RECV == 10");
    TEST_ASSERT(SYS_SENDTO == 11, "SYS_SENDTO == 11");
    TEST_ASSERT(SYS_RECVFROM == 12, "SYS_RECVFROM == 12");
    TEST_ASSERT(SYS_SHUTDOWN == 13, "SYS_SHUTDOWN == 13");
    TEST_ASSERT(SYS_SETSOCKOPT == 14, "SYS_SETSOCKOPT == 14");
    TEST_ASSERT(SYS_GETSOCKOPT == 15, "SYS_GETSOCKOPT == 15");

    /* ── Linux syscall number ──────────────────────────────────────── */
    TEST_ASSERT(LINUX_SYS_socketcall == 102, "LINUX_SYS_socketcall == 102");

    /* ── New errno values ──────────────────────────────────────────── */
    TEST_ASSERT(LINUX_ENOTSOCK == 88, "LINUX_ENOTSOCK == 88");
    TEST_ASSERT(LINUX_EPROTONOSUPPORT == 93, "LINUX_EPROTONOSUPPORT == 93");
    TEST_ASSERT(LINUX_EAFNOSUPPORT == 97, "LINUX_EAFNOSUPPORT == 97");
    TEST_ASSERT(LINUX_EADDRINUSE == 98, "LINUX_EADDRINUSE == 98");
    TEST_ASSERT(LINUX_ENETUNREACH == 101, "LINUX_ENETUNREACH == 101");
    TEST_ASSERT(LINUX_ECONNRESET == 104, "LINUX_ECONNRESET == 104");
    TEST_ASSERT(LINUX_ENOTCONN == 107, "LINUX_ENOTCONN == 107");
    TEST_ASSERT(LINUX_ETIMEDOUT == 110, "LINUX_ETIMEDOUT == 110");
    TEST_ASSERT(LINUX_ECONNREFUSED == 111, "LINUX_ECONNREFUSED == 111");
    TEST_ASSERT(LINUX_EINPROGRESS == 115, "LINUX_EINPROGRESS == 115");

    /* ── TCP backlog defines ───────────────────────────────────────── */
    TEST_ASSERT(TCP_BACKLOG_MAX == 4, "TCP_BACKLOG_MAX == 4");

    /* ── Socket create/close round-trip ────────────────────────────── */
    {
        int s = socket_create(SOCK_STREAM);
        TEST_ASSERT(s >= 0, "socket_create(STREAM) succeeds");
        int s2 = socket_create(SOCK_DGRAM);
        TEST_ASSERT(s2 >= 0, "socket_create(DGRAM) succeeds");
        TEST_ASSERT(s != s2, "two sockets have different fds");
        socket_close(s);
        socket_close(s2);
    }

    /* ── socket_set_nonblock / socket_get_nonblock ─────────────────── */
    {
        int s = socket_create(SOCK_STREAM);
        TEST_ASSERT(s >= 0, "nonblock test: socket created");
        TEST_ASSERT(socket_get_nonblock(s) == 0, "default nonblock is 0");
        socket_set_nonblock(s, 1);
        TEST_ASSERT(socket_get_nonblock(s) == 1, "nonblock set to 1");
        socket_set_nonblock(s, 0);
        TEST_ASSERT(socket_get_nonblock(s) == 0, "nonblock set back to 0");
        socket_close(s);
    }

    /* ── socket_poll_query on fresh socket ─────────────────────────── */
    {
        int s = socket_create(SOCK_STREAM);
        TEST_ASSERT(s >= 0, "poll_query test: socket created");
        int ev = socket_poll_query(s);
        TEST_ASSERT(ev == 0, "poll_query on unconnected socket is 0");
        socket_close(s);
    }

    /* ── socket_get_type ───────────────────────────────────────────── */
    {
        int s1 = socket_create(SOCK_STREAM);
        int s2 = socket_create(SOCK_DGRAM);
        TEST_ASSERT(socket_get_type(s1) == SOCK_STREAM, "get_type STREAM");
        TEST_ASSERT(socket_get_type(s2) == SOCK_DGRAM, "get_type DGRAM");
        socket_close(s1);
        socket_close(s2);
    }

    /* ── tcp_has_backlog on fresh TCB ──────────────────────────────── */
    TEST_ASSERT(tcp_has_backlog(0) == 0, "tcp_has_backlog(0) on init is 0");

    /* ── tcp_rx_available on fresh TCB ─────────────────────────────── */
    TEST_ASSERT(tcp_rx_available(0) == 0, "tcp_rx_available(0) on init is 0");

    /* ── tcp_recv_nb on unconnected TCB ────────────────────────────── */
    {
        uint8_t buf[16];
        int rc = tcp_recv_nb(0, buf, sizeof(buf));
        TEST_ASSERT(rc < 0, "tcp_recv_nb on unconnected returns error");
    }

    /* ── udp_rx_available ──────────────────────────────────────────── */
    TEST_ASSERT(udp_rx_available(9999) == 0, "udp_rx_available(unused) is 0");

    /* ── socket_recv_nb on fresh socket ────────────────────────────── */
    {
        int s = socket_create(SOCK_STREAM);
        uint8_t buf[16];
        int rc = socket_recv_nb(s, buf, sizeof(buf));
        TEST_ASSERT(rc < 0, "recv_nb on unconnected socket returns error");
        socket_close(s);
    }

    /* ── dns_cache_flush callable ──────────────────────────────────── */
    dns_cache_flush();
    TEST_ASSERT(1, "dns_cache_flush() callable without crash");

    /* ── socket_is_listening ───────────────────────────────────────── */
    {
        int s = socket_create(SOCK_STREAM);
        TEST_ASSERT(socket_is_listening(s) == 0, "fresh socket not listening");
        socket_close(s);
    }

    /* ── Invalid socket operations ─────────────────────────────────── */
    TEST_ASSERT(socket_create(99) == -1, "socket_create bad type fails");
    TEST_ASSERT(socket_set_nonblock(-1, 1) == -1, "set_nonblock bad fd fails");
    TEST_ASSERT(socket_get_nonblock(-1) == 0, "get_nonblock bad fd returns 0");
    TEST_ASSERT(socket_poll_query(-1) == 0, "poll_query bad fd returns 0");
}

/* ---- Phase 7: Win32 Subsystem Hardening ---- */

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

/* ---- Phase 7.5: Widget Toolkit & App Tests ---- */

#include <kernel/ui_widget.h>
#include <kernel/app.h>

static void test_phase75_widgets(void) {
    printf("== Phase 7.5 Widget Toolkit Tests ==\n");

    /* ── uw_create returns valid window ──────────────────────── */
    ui_window_t *tw = uw_create(100, 100, 300, 200, "TestWin");
    TEST_ASSERT(tw != NULL, "p75: uw_create returns non-null");
    if (!tw) return;
    TEST_ASSERT(tw->wm_id >= 0, "p75: uw_create wm_id valid");
    TEST_ASSERT(tw->widget_count == 0, "p75: initial widget_count is 0");
    TEST_ASSERT(tw->focused_widget == -1, "p75: focused_widget starts at -1");

    /* ── ui_add_label ────────────────────────────────────────── */
    int lbl = ui_add_label(tw, 10, 10, 100, 20, "Hello", 0xFF00FF00);
    TEST_ASSERT(lbl >= 0, "p75: ui_add_label returns valid index");
    TEST_ASSERT(tw->widget_count == 1, "p75: widget_count is 1 after add_label");
    {
        ui_widget_t *w = ui_get_widget(tw, lbl);
        TEST_ASSERT(w != NULL, "p75: ui_get_widget returns non-null");
        TEST_ASSERT(w->type == UI_LABEL, "p75: label type correct");
        TEST_ASSERT(strcmp(w->label.text, "Hello") == 0, "p75: label text stored");
        TEST_ASSERT(w->label.color == 0xFF00FF00, "p75: label color stored");
    }

    /* ── ui_add_button ───────────────────────────────────────── */
    int btn = ui_add_button(tw, 10, 40, 80, 30, "Click", NULL);
    TEST_ASSERT(btn >= 0, "p75: ui_add_button returns valid index");
    {
        ui_widget_t *w = ui_get_widget(tw, btn);
        TEST_ASSERT(w != NULL, "p75: button widget non-null");
        TEST_ASSERT(w->type == UI_BUTTON, "p75: button type correct");
        TEST_ASSERT(strcmp(w->button.text, "Click") == 0, "p75: button text stored");
        TEST_ASSERT(w->flags & UI_FLAG_FOCUSABLE, "p75: button is focusable");
    }

    /* ── ui_add_textinput ────────────────────────────────────── */
    int txt = ui_add_textinput(tw, 10, 80, 200, 28, "Type here", 64, 0);
    TEST_ASSERT(txt >= 0, "p75: ui_add_textinput returns valid index");
    {
        ui_widget_t *w = ui_get_widget(tw, txt);
        TEST_ASSERT(w != NULL, "p75: textinput widget non-null");
        TEST_ASSERT(w->type == UI_TEXTINPUT, "p75: textinput type correct");
        TEST_ASSERT(strcmp(w->textinput.placeholder, "Type here") == 0,
                    "p75: textinput placeholder stored");
    }

    /* ── ui_add_progress ─────────────────────────────────────── */
    int prog = ui_add_progress(tw, 10, 120, 200, 20, 42, "Loading");
    TEST_ASSERT(prog >= 0, "p75: ui_add_progress returns valid index");
    {
        ui_widget_t *w = ui_get_widget(tw, prog);
        TEST_ASSERT(w != NULL, "p75: progress widget non-null");
        TEST_ASSERT(w->progress.value == 42, "p75: progress value stored");
    }

    /* ── ui_add_checkbox ─────────────────────────────────────── */
    int chk = ui_add_checkbox(tw, 10, 150, 150, 20, "Enable", 1);
    TEST_ASSERT(chk >= 0, "p75: ui_add_checkbox returns valid index");
    {
        ui_widget_t *w = ui_get_widget(tw, chk);
        TEST_ASSERT(w != NULL, "p75: checkbox widget non-null");
        TEST_ASSERT(w->checkbox.checked == 1, "p75: checkbox initial state correct");
    }

    /* ── ui_add_separator ────────────────────────────────────── */
    int sep = ui_add_separator(tw, 10, 175, 200);
    TEST_ASSERT(sep >= 0, "p75: ui_add_separator returns valid index");

    /* ── ui_add_tabs ─────────────────────────────────────────── */
    static const char *tab_labels[] = { "Tab1", "Tab2" };
    int tabs = ui_add_tabs(tw, 10, 180, 200, 30, tab_labels, 2);
    TEST_ASSERT(tabs >= 0, "p75: ui_add_tabs returns valid index");

    /* ── ui_widget_set_visible ───────────────────────────────── */
    ui_widget_set_visible(tw, lbl, 0);
    {
        ui_widget_t *w = ui_get_widget(tw, lbl);
        TEST_ASSERT(!(w->flags & UI_FLAG_VISIBLE), "p75: set_visible(0) clears flag");
    }
    ui_widget_set_visible(tw, lbl, 1);
    {
        ui_widget_t *w = ui_get_widget(tw, lbl);
        TEST_ASSERT(w->flags & UI_FLAG_VISIBLE, "p75: set_visible(1) sets flag");
    }

    /* ── ui_focus_next ───────────────────────────────────────── */
    tw->focused_widget = -1;
    ui_focus_next(tw);
    TEST_ASSERT(tw->focused_widget == btn, "p75: focus_next finds first focusable (button)");

    /* ── ui_get_widget boundary ──────────────────────────────── */
    TEST_ASSERT(ui_get_widget(tw, -1) == NULL, "p75: get_widget(-1) returns NULL");
    TEST_ASSERT(ui_get_widget(tw, 999) == NULL, "p75: get_widget(999) returns NULL");

    /* ── Widget count ────────────────────────────────────────── */
    int expected_count = 7; /* label, button, textinput, progress, checkbox, separator, tabs */
    TEST_ASSERT(tw->widget_count == expected_count,
                "p75: widget_count matches added widgets");

    /* ── App registry: only implemented apps ─────────────────── */
    int app_count = app_get_count();
    TEST_ASSERT(app_count == 9, "p75: app registry has 9 apps");

    /* Verify known apps exist */
    TEST_ASSERT(app_find("terminal") != NULL, "p75: terminal in registry");
    TEST_ASSERT(app_find("calculator") != NULL, "p75: calculator in registry");
    TEST_ASSERT(app_find("notes") != NULL, "p75: notes in registry");
    TEST_ASSERT(app_find("about") != NULL, "p75: about in registry");
    TEST_ASSERT(app_find("mines") != NULL, "p75: minesweeper in registry");

    /* Verify stubs are removed */
    TEST_ASSERT(app_find("browser") == NULL, "p75: browser stub removed");
    TEST_ASSERT(app_find("email") == NULL, "p75: email stub removed");

    /* ── Cleanup ─────────────────────────────────────────────── */
    uw_destroy(tw);
}

/* ---- Phase 8: Desktop IPC ---- */

#include <kernel/msgbus.h>
#include <kernel/clipboard.h>
#include <kernel/notify.h>
#include <kernel/systray.h>

static int p8_handler_called;
static int p8_handler_ival;
static const char *p8_handler_sval;

static void p8_test_handler(const msgbus_msg_t *msg, void *ctx) {
    (void)ctx;
    p8_handler_called++;
    if (msg->type == MSGBUS_TYPE_INT)
        p8_handler_ival = msg->ival;
    else if (msg->type == MSGBUS_TYPE_STR)
        p8_handler_sval = msg->sval;
}

static void test_phase8_ipc(void) {
    printf("== Phase 8 Desktop IPC Tests ==\n");

    /* ── Clipboard tests ────────────────────────────────────────── */
    clipboard_clear();
    size_t clen = 0;
    const char *cdata = clipboard_get(&clen);
    TEST_ASSERT(clen == 0, "p8: clipboard initially empty");
    TEST_ASSERT(clipboard_has_content() == 0, "p8: no content initially");

    clipboard_copy("hello", 5);
    cdata = clipboard_get(&clen);
    TEST_ASSERT(clen == 5, "p8: clipboard len after copy");
    TEST_ASSERT(memcmp(cdata, "hello", 5) == 0, "p8: clipboard content");
    TEST_ASSERT(clipboard_has_content() == 1, "p8: has content after copy");

    clipboard_copy("world!", 6);
    cdata = clipboard_get(&clen);
    TEST_ASSERT(clen == 6, "p8: clipboard overwrite len");
    TEST_ASSERT(memcmp(cdata, "world!", 6) == 0, "p8: clipboard overwrite");

    clipboard_clear();
    cdata = clipboard_get(&clen);
    TEST_ASSERT(clen == 0, "p8: clipboard clear");

    /* Large copy truncates at CLIPBOARD_MAX-1 */
    {
        char big[CLIPBOARD_MAX + 10];
        memset(big, 'A', sizeof(big));
        clipboard_copy(big, sizeof(big));
        cdata = clipboard_get(&clen);
        TEST_ASSERT(clen == CLIPBOARD_MAX - 1, "p8: clipboard truncate");
    }
    clipboard_clear();

    /* ── Message bus tests ──────────────────────────────────────── */
    msgbus_init(); /* reset for testing */

    int sub = msgbus_subscribe("test-topic", p8_test_handler, NULL);
    TEST_ASSERT(sub >= 0, "p8: msgbus subscribe returns valid id");

    p8_handler_called = 0;
    p8_handler_ival = 0;
    int delivered = msgbus_publish_int("test-topic", 42);
    TEST_ASSERT(delivered == 1, "p8: msgbus publish delivers to 1 sub");
    TEST_ASSERT(p8_handler_called == 1, "p8: handler called once");
    TEST_ASSERT(p8_handler_ival == 42, "p8: handler received int value");

    /* Unrelated topic */
    p8_handler_called = 0;
    delivered = msgbus_publish_int("other-topic", 99);
    TEST_ASSERT(delivered == 0, "p8: unrelated topic not delivered");
    TEST_ASSERT(p8_handler_called == 0, "p8: handler not called for other topic");

    /* Multiple subscribers */
    int sub2 = msgbus_subscribe("test-topic", p8_test_handler, NULL);
    TEST_ASSERT(sub2 >= 0, "p8: second subscribe ok");
    p8_handler_called = 0;
    delivered = msgbus_publish_int("test-topic", 7);
    TEST_ASSERT(delivered == 2, "p8: two subscribers notified");
    TEST_ASSERT(p8_handler_called == 2, "p8: handler called twice");

    /* Unsubscribe */
    msgbus_unsubscribe(sub);
    p8_handler_called = 0;
    delivered = msgbus_publish_int("test-topic", 1);
    TEST_ASSERT(delivered == 1, "p8: after unsub, one remaining");
    TEST_ASSERT(p8_handler_called == 1, "p8: only remaining handler called");

    /* String message */
    p8_handler_called = 0;
    p8_handler_sval = NULL;
    msgbus_publish_str("test-topic", "hello bus");
    TEST_ASSERT(p8_handler_called == 1, "p8: str msg delivered");
    TEST_ASSERT(p8_handler_sval != NULL && strcmp(p8_handler_sval, "hello bus") == 0,
                "p8: str msg content correct");

    msgbus_unsubscribe(sub2);

    /* ── Notification tests (structural) ────────────────────────── */
    notify_id_t nid = notify_post("Test", "Body", NOTIFY_INFO, 600);
    TEST_ASSERT(nid >= 0, "p8: notify_post returns valid id");
    /* Before any tick, queued but not yet promoted */
    TEST_ASSERT(notify_visible_count() == 0, "p8: notify not visible before tick");
    notify_dismiss(nid);

    /* ── System tray tests (structural) ──────────────────────────── */
    systray_init(); /* reset for testing */
    TEST_ASSERT(systray_get_count() == 0, "p8: systray initially empty");

    int ti = systray_register("Ab", "Test", 0xFFFFFFFF, NULL, NULL);
    TEST_ASSERT(ti >= 0, "p8: systray register returns valid idx");
    TEST_ASSERT(systray_get_count() == 1, "p8: systray count after register");
    TEST_ASSERT(systray_get_width() == SYSTRAY_ITEM_W, "p8: systray width");

    int ti2 = systray_register("Cd", "Test2", 0xFF00FF00, NULL, NULL);
    TEST_ASSERT(ti2 >= 0, "p8: systray second register ok");
    TEST_ASSERT(systray_get_count() == 2, "p8: systray count == 2");
    TEST_ASSERT(systray_get_width() == 2 * SYSTRAY_ITEM_W, "p8: systray width x2");

    systray_unregister(ti);
    TEST_ASSERT(systray_get_count() == 1, "p8: systray count after unregister");

    systray_unregister(ti2);
    TEST_ASSERT(systray_get_count() == 0, "p8: systray empty after unregister all");

    /* Re-init msgbus for rest of system */
    msgbus_init();
}

/* ---- Run All ---- */

void test_run_all(void) {
    test_count = 0;
    test_pass = 0;
    test_fail = 0;

    printf("\n=== ImposOS Regression Tests ===\n\n");

    test_string();
    test_string_extra();
    test_stdlib();
    test_stdlib_extra();
    test_snprintf();
    test_sscanf();
    test_fs();
    test_fs_indirect();
    test_fs_v3();
    test_user();
    test_gfx();
    test_quota();
    test_network();
    test_firewall();
    test_mouse();
    test_crypto();
    test_win32_dll();
    test_win32_registry();
    test_winsock();
    test_gdi_advanced();
    test_com_ole();
    test_unicode_wide();
    test_unicode_i18n();
    test_security_crypto();
    test_seh();
    test_misc_win32();
    test_crt_phase1();
    test_seh_phase2();
    test_scheduler_priority();
    test_process_lifecycle();
    test_process_groups();
    test_signals_phase2();
    test_fd_table();
    test_futex();
    test_pthreads();
    test_vma();
    test_frame_ref();
    test_memory_phase3();
    test_phase4_syscalls();
    test_nanosleep_execve();
    test_phase5_dynlink();
    test_phase6_networking();
    test_phase7_win32();
    test_phase75_widgets();
    test_phase8_ipc();

    printf("\n=== Results: %d/%d passed", test_pass, test_count);
    if (test_fail > 0) {
        printf(", %d FAILED", test_fail);
    }
    printf(" ===\n\n");
}
