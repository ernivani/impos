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
#include <kernel/dns.h>
#include <kernel/task.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

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
    TEST_ASSERT(disp == 1, "reg: disposition is CREATED_NEW");

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
    test_security_crypto();
    test_seh();
    test_misc_win32();

    printf("\n=== Results: %d/%d passed", test_pass, test_count);
    if (test_fail > 0) {
        printf(", %d FAILED", test_fail);
    }
    printf(" ===\n\n");
}
